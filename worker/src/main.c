#include "vw_worker.h"
#include "vw_worker_config.h"

int main(int argc, char** argv) {
  vw_worker_config_t config;
  vw_worker_config_init_defaults(&config);  // zeros auth_token, sets model/language/rate

  int parse_rc = vw_worker_config_parse_args(&config, argc, argv);
  if (parse_rc != 0) {
    return parse_rc;
  }
  return vw_worker_run(&config);
}
