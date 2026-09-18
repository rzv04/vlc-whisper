#ifndef VW_SETTINGS_FILE_H
#define VW_SETTINGS_FILE_H

#include <stddef.h>
#include <stdint.h>

// Returns a heap string for an external settings override, taking ownership of fallback.
// Unknown/unavailable settings return fallback unchanged; caller keeps normal config_GetPsz ownership semantics.
char* vw_settings_override_psz(const char* key, char* fallback);

// Returns a validated external settings value when present, otherwise fallback.
int64_t vw_settings_override_int(const char* key, int64_t fallback);

// Deletes model-command only when it still matches the command that was actually consumed.
void vw_settings_ack_model_command(const char* consumed);

// Mirrors small runtime status values for the standalone settings UI. No network or model hashing occurs here.
void vw_settings_note_psz(const char* key, const char* value);
void vw_settings_note_int(const char* key, int64_t value);

#endif