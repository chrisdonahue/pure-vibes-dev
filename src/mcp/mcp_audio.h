/* mcp_audio.h - Audio stream capture for MCP tools
 *
 * Captures per-block audio features from Pd's output buffer into a
 * circular buffer. Called from sched_tick() after dsp_tick().
 * Query functions are called from MCP tool handlers on the main thread.
 *
 * Threading: In polling mode both writer and reader run on the main
 * thread. In callback mode both run under sys_lock(). No additional
 * synchronization is needed.
 */

#ifndef MCP_AUDIO_H
#define MCP_AUDIO_H

#include "m_pd.h"  /* t_sample, t_float */

/* Maximum output channels we track. Extra channels are silently ignored. */
#define MCP_AUDIO_MAX_CHANNELS 32

/* Initialize the audio capture module (allocates ring buffer). */
void mcp_audio_init(void);

/* Free all resources. */
void mcp_audio_free(void);

/* Called every DSP tick right after dsp_tick() fills st_soundout.
 *
 *   soundout  - STUFF->st_soundout (channel-major, blocksize per channel)
 *   nchannels - STUFF->st_outchannels
 *   blocksize - DEFDACBLKSIZE (typically 64)
 *   sr        - STUFF->st_dacsr
 *
 * Must be fast — runs ~750 times/sec at 48 kHz / 64.
 */
void mcp_audio_capture(const t_sample *soundout, int nchannels,
    int blocksize, t_float sr);

/* Per-channel RMS statistics returned by mcp_audio_query_rms(). */
typedef struct _mcp_audio_rms_stats {
    int channels;             /* number of channels captured */
    int blocks_used;          /* blocks actually averaged */
    float actual_duration;    /* seconds of data used */
    float sample_rate;
    int block_size;
    float mean_rms[MCP_AUDIO_MAX_CHANNELS];
    float var_rms[MCP_AUDIO_MAX_CHANNELS];
    float min_rms[MCP_AUDIO_MAX_CHANNELS];
    float max_rms[MCP_AUDIO_MAX_CHANNELS];
} t_mcp_audio_rms_stats;

/* Query RMS statistics over the last `duration` seconds.
 * Returns 0 on success, -1 if no audio data is available. */
int mcp_audio_query_rms(float duration, t_mcp_audio_rms_stats *stats);

#endif /* MCP_AUDIO_H */
