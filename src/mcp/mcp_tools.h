/* mcp_tools.h - Shared MCP tool definitions and protocol constants
 *
 * Used by both the MCP server (linked into Pd) and the standalone
 * proxy binary (pd-mcp). Depends only on cJSON, no Pd internals.
 */

#ifndef MCP_TOOLS_H
#define MCP_TOOLS_H

#include "cJSON.h"

/* protocol constants */
#define MCP_PROTOCOL_VERSION "2025-03-26"
#define MCP_SERVER_NAME      "pd-vibes"
#define MCP_SERVER_VERSION   "0.56.2"
#define MCP_DEFAULT_PORT     4330

#define MCP_INSTRUCTIONS \
    "You are controlling Pd-vibes (Pure Vibes), a fork of Pure Data — " \
    "a visual programming language for audio and multimedia. " \
    "Patches contain objects (audio/control processors) connected by " \
    "patch cords from outlets to inlets. Objects with a '~' suffix " \
    "(e.g. osc~, dac~) process audio signals; others process control " \
    "messages. Use get_patch_state to inspect a patch before modifying " \
    "it. Prefer batch_update over individual create/connect/delete calls " \
    "when making multiple changes — it is atomic and faster. " \
    "Use get_object_doc to look up any unfamiliar object. " \
    "DSP must be on (set_dsp) for audio to flow. " \
    "If any tool call fails or returns a connection error, ask the user " \
    "to check that Pd-vibes is running and the MCP checkbox is enabled " \
    "(Media > MCP Server)."

/* JSON schema helpers */
cJSON *mcp_prop_string(const char *desc);
cJSON *mcp_prop_int(const char *desc);
cJSON *mcp_prop_number(const char *desc);
cJSON *mcp_prop_bool(const char *desc);
cJSON *mcp_make_schema(const char *required[], int nrequired);
void mcp_schema_add_prop(cJSON *schema, const char *name, cJSON *prop);

/* build the complete MCP tools list as a cJSON array */
cJSON *mcp_build_tools_list(void);

#endif /* MCP_TOOLS_H */
