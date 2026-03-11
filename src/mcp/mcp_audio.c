/* mcp_audio.c - Audio stream capture for MCP tools
 *
 * Maintains a circular buffer of per-block audio features computed from
 * Pd's output buffer (STUFF->st_soundout). The buffer is written once
 * per DSP tick and read by MCP tool handlers on demand.
 *
 * TODO (future): Add a raw-sample ring buffer (~60 s, ~23 MB stereo at
 * 48 kHz) to enable audio-clip export and spectrogram tools. Store raw
 * t_sample data in channel-major layout matching st_soundout. The
 * capture hook is already in place — just needs a second ring and a
 * memcpy in mcp_audio_capture().
 */

#include "mcp_audio.h"
#include <string.h>
#include <math.h>

/* ---- per-block feature record ---- */

typedef struct _mcp_audio_block {
    float mean_of_squares[MCP_AUDIO_MAX_CHANNELS];
    /* future: float peak[MCP_AUDIO_MAX_CHANNELS]; */
    /* future: float spectral_centroid[MCP_AUDIO_MAX_CHANNELS]; */
} t_mcp_audio_block;

/* ---- module state (file-static singleton) ---- */

/* Capacity: ~60 s at 96 kHz / 64, ~120 s at 48 kHz / 64. */
#define MCP_AUDIO_RING_CAPACITY 90000

static struct {
    t_mcp_audio_block *ring;   /* circular buffer */
    int capacity;              /* always MCP_AUDIO_RING_CAPACITY */
    int write_pos;             /* next write index (wraps) */
    int count;                 /* entries written (capped at capacity) */
    int channels;              /* channels at last capture */
    float sample_rate;         /* sample rate at last capture */
    int block_size;            /* block size at last capture */
    int initialized;
} mcp_audio;

/* ---- lifecycle ---- */

void mcp_audio_init(void)
{
    if (mcp_audio.initialized)
        return;
    mcp_audio.capacity = MCP_AUDIO_RING_CAPACITY;
    mcp_audio.ring = (t_mcp_audio_block *)getbytes(
        mcp_audio.capacity * sizeof(t_mcp_audio_block));
    if (!mcp_audio.ring)
    {
        post("mcp_audio: failed to allocate capture buffer");
        return;
    }
    memset(mcp_audio.ring, 0,
        mcp_audio.capacity * sizeof(t_mcp_audio_block));
    mcp_audio.write_pos = 0;
    mcp_audio.count = 0;
    mcp_audio.channels = 0;
    mcp_audio.sample_rate = 0;
    mcp_audio.block_size = 0;
    mcp_audio.initialized = 1;
}

void mcp_audio_free(void)
{
    if (mcp_audio.ring)
    {
        freebytes(mcp_audio.ring,
            mcp_audio.capacity * sizeof(t_mcp_audio_block));
        mcp_audio.ring = NULL;
    }
    mcp_audio.initialized = 0;
}

/* ---- capture (hot path) ---- */

void mcp_audio_capture(const t_sample *soundout, int nchannels,
    int blocksize, t_float sr)
{
    if (!mcp_audio.initialized || !mcp_audio.ring)
        return;

    /* stop capturing when DSP is off to preserve last real audio
       and avoid recording stale data from st_soundout */
    if (!canvas_dspstate)
        return;

    /* detect audio-config change and reset */
    if (nchannels != mcp_audio.channels ||
        (float)sr != mcp_audio.sample_rate ||
        blocksize != mcp_audio.block_size)
    {
        mcp_audio.channels = nchannels;
        mcp_audio.sample_rate = (float)sr;
        mcp_audio.block_size = blocksize;
        mcp_audio.write_pos = 0;
        mcp_audio.count = 0;
        memset(mcp_audio.ring, 0,
            mcp_audio.capacity * sizeof(t_mcp_audio_block));
    }

    int nch = nchannels < MCP_AUDIO_MAX_CHANNELS
        ? nchannels : MCP_AUDIO_MAX_CHANNELS;

    t_mcp_audio_block *blk = &mcp_audio.ring[mcp_audio.write_pos];
    float inv_n = 1.0f / blocksize;
    int ch, i;

    for (ch = 0; ch < nch; ch++)
    {
        const t_sample *buf = soundout + (long)blocksize * ch;
        float sum_sq = 0.0f;
        for (i = 0; i < blocksize; i++)
        {
            float s = (float)buf[i];
            sum_sq += s * s;
        }
        blk->mean_of_squares[ch] = sum_sq * inv_n;
    }
    /* zero unused channel slots */
    for (; ch < MCP_AUDIO_MAX_CHANNELS; ch++)
        blk->mean_of_squares[ch] = 0.0f;

    mcp_audio.write_pos =
        (mcp_audio.write_pos + 1) % mcp_audio.capacity;
    if (mcp_audio.count < mcp_audio.capacity)
        mcp_audio.count++;
}

/* ---- query ---- */

int mcp_audio_query_rms(float duration, t_mcp_audio_rms_stats *stats)
{
    if (!mcp_audio.initialized || mcp_audio.count == 0 ||
        mcp_audio.sample_rate <= 0)
        return -1;

    int nch = mcp_audio.channels < MCP_AUDIO_MAX_CHANNELS
        ? mcp_audio.channels : MCP_AUDIO_MAX_CHANNELS;

    float blocks_per_sec = mcp_audio.sample_rate / mcp_audio.block_size;
    int requested = (int)(duration * blocks_per_sec + 0.5f);
    if (requested > mcp_audio.count)
        requested = mcp_audio.count;
    if (requested < 1)
        requested = 1;

    /* oldest block in the requested range */
    int start = (mcp_audio.write_pos - requested
        + mcp_audio.capacity) % mcp_audio.capacity;

    stats->channels = nch;
    stats->blocks_used = requested;
    stats->actual_duration = requested / blocks_per_sec;
    stats->sample_rate = mcp_audio.sample_rate;
    stats->block_size = mcp_audio.block_size;

    int ch, b;
    for (ch = 0; ch < nch; ch++)
    {
        float sum_mos = 0.0f;   /* sum of mean_of_squares */
        float min_rms = 1e30f;
        float max_rms = 0.0f;
        float sum_rms = 0.0f;   /* for variance: E[rms] */
        float sum_rms2 = 0.0f;  /* for variance: E[rms^2] */

        for (b = 0; b < requested; b++)
        {
            int idx = (start + b) % mcp_audio.capacity;
            float mos = mcp_audio.ring[idx].mean_of_squares[ch];
            float rms = sqrtf(mos);
            sum_mos += mos;
            sum_rms += rms;
            sum_rms2 += rms * rms;
            if (rms < min_rms) min_rms = rms;
            if (rms > max_rms) max_rms = rms;
        }

        /* global RMS over entire duration */
        stats->mean_rms[ch] = sqrtf(sum_mos / requested);
        stats->min_rms[ch] = min_rms;
        stats->max_rms[ch] = max_rms;

        /* variance of per-block RMS: Var = E[x^2] - E[x]^2 */
        float mean_block_rms = sum_rms / requested;
        stats->var_rms[ch] = (sum_rms2 / requested)
            - (mean_block_rms * mean_block_rms);
        if (stats->var_rms[ch] < 0.0f)
            stats->var_rms[ch] = 0.0f;  /* numerical safety */
    }

    return 0;
}
