#ifndef VW_SETTINGS_SERVICE_H_
#define VW_SETTINGS_SERVICE_H_

// Runs one settings-owned operation over inherited private stdin/stdout pipes, without playback IPC or ASR startup.
// Returns 0 only for completed success, 2 for invalid input, 3 for cancellation, and 4 for operation/transport failure.
int vw_settings_service_run(int argc, char** argv);

#endif  // VW_SETTINGS_SERVICE_H_
