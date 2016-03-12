/*
 * Copyright (c) 2015 Cesanta Software Limited
 * All rights reserved
 */

#include "esp_common.h"

#include "mongoose.h"

#define AP_SSID "Mongoose"
#define AP_PASS "Mongoose"
#define AP_CHAN 9
#define MG_LISTEN_ADDR "80"

#define MG_TASK_STACK_SIZE 2048
#define MG_TASK_PRIORITY 1

void uart_div_modify(int uart_no, unsigned int freq);

void ev_handler(struct mg_connection *nc, int ev, void *p) {
  static const char *reply_fmt =
      "HTTP/1.0 200 OK\r\n"
      "Connection: close\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "Hello %s\n";
  LOG(LL_DEBUG, ("conn %p ev %d", nc, ev));

  switch (ev) {
    case MG_EV_ACCEPT: {
      char addr[32];
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
                          MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      LOG(LL_INFO, ("Connection %p from %s", nc, addr));
      break;
    }
    case MG_EV_HTTP_REQUEST: {
      char addr[32];
      struct http_message *hm = (struct http_message *) p;
      (void) hm;
      mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
                          MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
      LOG(LL_INFO,
          ("HTTP request from %s: %.*s %.*s", addr, (int) hm->method.len,
           hm->method.p, (int) hm->uri.len, hm->uri.p));
      mg_printf(nc, reply_fmt, addr);
      nc->flags |= MG_F_SEND_AND_CLOSE;
      break;
    }
    case MG_EV_CLOSE: {
      LOG(LL_INFO, ("Connection %p closed", nc));
      break;
    }
  }
}

void setup_ap() {
  struct softap_config cfg;

  wifi_set_opmode_current(SOFTAP_MODE);
  memset(&cfg, 0, sizeof(cfg));
  strcpy((char *) cfg.ssid, AP_SSID);
  strcpy((char *) cfg.password, AP_PASS);
  cfg.ssid_len = strlen((const char *) cfg.ssid);
  cfg.authmode =
      strlen((const char *) cfg.password) ? AUTH_WPA2_PSK : AUTH_OPEN;
  cfg.channel = AP_CHAN;
  cfg.ssid_hidden = 0;
  cfg.max_connection = 10;
  cfg.beacon_interval = 100; /* ms */

  LOG(LL_INFO, ("Setting up AP '%s' on channel %d", cfg.ssid, cfg.channel));
  wifi_softap_set_config_current(&cfg);
}

static void mg_task(void *arg) {
  struct mg_mgr mgr;
  struct mg_connection *nc;

  cs_log_set_level(LL_INFO);

  LOG(LL_INFO, ("SDK version: %s", system_get_sdk_version()));
  setup_ap();

  mg_mgr_init(&mgr, NULL);

  nc = mg_bind(&mgr, MG_LISTEN_ADDR, ev_handler);
  if (nc == NULL) {
    LOG(LL_ERROR, ("Error setting up listener!"));
    return;
  }
  mg_set_protocol_http_websocket(nc);

  while (1) {
    mg_mgr_poll(&mgr, 1000);
  }
}

xTaskHandle s_mg_task_handle;

void user_init(void) {
  uart_div_modify(0, UART_CLK_FREQ / 115200);

//  setvbuf(stdout, NULL, _IONBF, 0);
//  setvbuf(stderr, NULL, _IONBF, 0);

  xTaskCreate(mg_task, (const signed char *) "mongoose", MG_TASK_STACK_SIZE,
              NULL, MG_TASK_PRIORITY, &s_mg_task_handle);
}

void call_user_start(void) {
  user_init();
}
