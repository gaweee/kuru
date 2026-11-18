#include "kuru_console.h"
#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "wifi_creds.h"
#include "wifi_link.h"

// `wifi` command of the USB console. Quote a name that has spaces: wifi set "My Network" "my password".
static int cmd_wifi(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "status";
    if (!strcmp(sub, "set") && (argc == 3 || argc == 4)) {
        const char *ssid = argv[2], *pass = argc == 4 ? argv[3] : "";
        const wifi_creds_err_t e = wifi_creds_validate(ssid, pass);
        if (e != WIFI_CREDS_OK) { printf("not saved: %s\n", wifi_creds_error_text(e)); return 1; }
        if (wifi_link_set_credentials(ssid, pass) != ESP_OK) { printf("not saved: the flash write failed\n"); return 1; }
        printf("saved \"%s\"%s; connecting...\n", ssid, pass[0] ? "" : " (open network)");
        return 0;
    }
    if (!strcmp(sub, "clear")) { wifi_link_clear_credentials(); printf("credentials erased\n"); return 0; }
    if (!strcmp(sub, "status")) { char line[160]; wifi_link_describe(line, sizeof line); printf("%s\n", line); return 0; }
    if (!strcmp(sub, "scan")) { wifi_link_scan_print(argc > 2 ? argv[2] : NULL); return 0; }
    printf("usage:\n  wifi set <network> [password]   store the credentials and connect (leave the password out for an open network)\n"
           "  wifi clear                       erase the stored credentials\n  wifi status                      stored network, connection, address\n"
           "  wifi scan [network]              list the access points in range, with their BSSID (2.4 GHz only); one name: only its APs\n");
    return 1;
}

void kuru_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "kuru> ";
    repl_cfg.task_stack_size = 8192;          // the default 4096 overflowed while printing a scan
    repl_cfg.max_cmdline_length = 256;
    esp_console_dev_usb_serial_jtag_config_t dev = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    if (esp_console_new_repl_usb_serial_jtag(&dev, &repl_cfg, &repl) != ESP_OK) { ESP_LOGE("console", "could not start the USB console"); return; }
    const esp_console_cmd_t cmd = { .command = "wifi", .help = "Wi-Fi credentials and status (try: wifi)", .func = cmd_wifi };
    esp_console_cmd_register(&cmd);
    esp_console_register_help_command();
    esp_console_start_repl(repl);
}
