// AFE_CONFIG_DEFAULT() usa inicializadores designados aninhados (só C): fica num ficheiro .c.
#include "sdkconfig.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_afe_sr_iface.h"

void tessela_afe_default_config(afe_config_t *cfg) {
  afe_config_t def = AFE_CONFIG_DEFAULT();
  *cfg = def;
}
