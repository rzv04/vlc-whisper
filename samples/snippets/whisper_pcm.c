// A sample that demonstrates the whole Whisper VAD+timestamp transcription
// pipeline from an input PCM 16KHz mono buffer.

#include <stdio.h>
#include <whisper.h>

int main(int argc, char** argv) {
  // Unbuffer stdout & stderr so output prints immediately on Windows CMD
  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  const char* model_path = (argc > 1) ? argv[1] : "models/ggml-tiny.en.bin";

  printf("Loading Whisper model from: %s\n", model_path);

  struct whisper_context_params cparams = whisper_context_default_params();
  struct whisper_context* ctx = whisper_init_from_file_with_params(model_path, cparams);

  if (ctx == NULL) {
    printf("Failed to initialize Whisper context from '%s'\n", model_path);
    return 1;
  }

  printf("Whisper context initialized successfully\n");

  whisper_free(ctx);
  return 0;
}
