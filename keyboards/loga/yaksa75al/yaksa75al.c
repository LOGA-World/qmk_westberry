// Copyright 2024 yangzheng20003 (@yangzheng20003)
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "wls/wls.h"

#ifdef WIRELESS_ENABLE
#    include "wireless.h"
#    include "usb_main.h"
#    include "lowpower.h"
#endif

typedef union {
    uint32_t raw;
    struct {
        uint8_t flag : 1;
        uint8_t devs : 3;
        uint8_t chk_bat : 1;
        uint8_t record_last_mode;
        uint8_t last_btdevs : 3;
    };
} confinfo_t;
confinfo_t confinfo;

typedef struct {
    bool     active;
    uint32_t timer;
    uint32_t interval;
    uint32_t times;
    uint8_t  index;
    RGB      rgb;
    void (*blink_cb)(uint8_t);
} hs_rgb_indicator_t;

enum layers {
    _BL = 0,
    _FL,
    _MBL,
    _MFL,
    _FBL,
};

hs_rgb_indicator_t hs_rgb_indicators[HS_RGB_INDICATOR_COUNT];
hs_rgb_indicator_t hs_rgb_bat[HS_RGB_BAT_COUNT];

void rgb_blink_dir(void);
void hs_reset_settings(void);
void rgb_matrix_hs_indicator(void);
void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times);
void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time);

#define keymap_is_mac_system() ((get_highest_layer(default_layer_state) == _MBL) || (get_highest_layer(default_layer_state) == _MFL))
#define keymap_is_base_layer() ((get_highest_layer(default_layer_state) == _BL) || (get_highest_layer(default_layer_state) == _FL))

uint32_t        post_init_timer       = 0x00;
bool            inqbat_flag           = false;
bool            mac_status            = false;
bool            charging_state        = false;
bool            bat_full_flag         = false;
bool            enable_bat_indicators = true;
bool            rk_bat_req_flag       = false;
bool            charge_chk_flag       = false;
uint32_t        bat_indicator_cnt     = true;
static uint32_t ee_clr_timer          = 0;
bool            test_white_light_flag = false;
HSV             start_hsv;
bool            no_record_fg;
bool            lower_sleep = false;
uint8_t         buff[]      = {14, 8, 2, 1, 1, 1, 1, 1, 1, 1, 0};

void eeconfig_confinfo_update(uint32_t raw) {
    eeconfig_update_kb(raw);
}

uint32_t eeconfig_confinfo_read(void) {
    return eeconfig_read_kb();
}

void eeconfig_confinfo_default(void) {
    confinfo.flag             = true;
    confinfo.chk_bat          = false;
    confinfo.record_last_mode = 0xff;
    confinfo.last_btdevs      = 1;

    // #ifdef WIRELESS_ENABLE
    //     confinfo.devs = DEVS_USB;
    // #endif

    eeconfig_init_user_datablock();
    eeconfig_confinfo_update(confinfo.raw);

#ifdef RGBLIGHT_ENABLE
    rgblight_mode(buff[0]);
#endif
}

void eeconfig_confinfo_init(void) {
    confinfo.raw = eeconfig_confinfo_read();
    if (!confinfo.raw) {
        eeconfig_confinfo_default();
    }
}

void keyboard_post_init_kb(void) {
#ifdef CONSOLE_ENABLE
    debug_enable = true;
#endif

    eeconfig_confinfo_init();

#ifdef LED_POWER_EN_PIN
    gpio_set_pin_output(LED_POWER_EN_PIN);
    gpio_write_pin_high(LED_POWER_EN_PIN);
    gpio_set_pin_output(A9);
    gpio_write_pin_high(A9);

    gpio_set_pin_output(HS_LED_BOOSTING_PIN);
    gpio_write_pin_high(HS_LED_BOOSTING_PIN);
#endif

#ifdef MM_BT_DEF_PIN
    setPinInputHigh(MM_BT_DEF_PIN);
#endif

#ifdef MM_2G4_DEF_PIN
    setPinInputHigh(MM_2G4_DEF_PIN);
#endif

#ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
    gpio_set_pin_output(USB_POWER_EN_PIN);
#endif

#ifdef HS_BAT_CABLE_PIN
    setPinInput(HS_BAT_CABLE_PIN);
#endif

#ifdef BAT_FULL_PIN
    setPinInputHigh(BAT_FULL_PIN);
#endif

#ifdef WIRELESS_ENABLE
    wireless_init();
    // wireless_devs_change(!confinfo.devs, confinfo.devs, false);
    post_init_timer = timer_read32();
#endif

    keyboard_post_init_user();

    start_hsv = rgb_matrix_get_hsv();
}

#ifdef WIRELESS_ENABLE

void usb_power_connect(void) {
#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_low(USB_POWER_EN_PIN);
#    endif
}

void usb_power_disconnect(void) {
#    ifdef USB_POWER_EN_PIN
    gpio_write_pin_high(USB_POWER_EN_PIN);
#    endif
}

void suspend_power_down_kb(void) {
#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_low(LED_POWER_EN_PIN);
#    endif

    suspend_power_down_user();
}

void suspend_wakeup_init_kb(void) {
#    ifdef LED_POWER_EN_PIN
    gpio_write_pin_high(LED_POWER_EN_PIN);
#    endif

    wireless_devs_change(wireless_get_current_devs(), wireless_get_current_devs(), false);
    suspend_wakeup_init_user();
    hs_rgb_blink_set_timer(timer_read32());
}

bool lpwr_is_allow_timeout_hook(void) {
    if (wireless_get_current_devs() == DEVS_USB) {
        return false;
    }

    return true;
}

void wireless_post_task(void) {
    // auto switching devs
    if (post_init_timer && timer_elapsed32(post_init_timer) >= 100) {
        md_send_devctrl(MD_SND_CMD_DEVCTRL_FW_VERSION);   // get the module fw version.
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_BT_EN);  // timeout 30min to sleep in bt mode, enable
        md_send_devctrl(MD_SND_CMD_DEVCTRL_SLEEP_2G4_EN); // timeout 30min to sleep in 2.4g mode, enable
        wireless_devs_change(!confinfo.devs, confinfo.devs, false);
        post_init_timer = 0x00;
    }

    hs_mode_scan(false, confinfo.devs, confinfo.last_btdevs);
}

uint32_t wls_process_long_press(uint32_t trigger_time, void *cb_arg) {
    uint16_t keycode = *((uint16_t *)cb_arg);

    switch (keycode) {
        case KC_BT1: {
            uint8_t mode = confinfo.devs;
            hs_modeio_detection(true, &mode, confinfo.last_btdevs);
            if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT1, true);
            }

        } break;
        case KC_BT2: {
            uint8_t mode = confinfo.devs;
            hs_modeio_detection(true, &mode, confinfo.last_btdevs);
            if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT2, true);
            }
        } break;
        case KC_BT3: {
            uint8_t mode = confinfo.devs;
            hs_modeio_detection(true, &mode, confinfo.last_btdevs);
            if ((mode == hs_bt) || (mode == hs_wireless) || (mode == hs_none)) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_BT3, true);
            }
        } break;
        case KC_2G4: {
            uint8_t mode = confinfo.devs;
            hs_modeio_detection(true, &mode, confinfo.last_btdevs);
            if ((mode == hs_2g4) || (mode == hs_wireless) || (mode == hs_none)) {
                wireless_devs_change(wireless_get_current_devs(), DEVS_2G4, true);
            }
        } break;
        case EE_CLR: {
        } break;
        default:
            break;
    }

    return 0;
}

bool process_record_wls(uint16_t keycode, keyrecord_t *record) {
    static uint16_t       keycode_shadow               = 0x00;
    static deferred_token wls_process_long_press_token = INVALID_DEFERRED_TOKEN;

    // Early return for non-wireless keycodes to reduce processing overhead
    if (keycode != KC_BT1 && keycode != KC_BT2 && keycode != KC_BT3 && keycode != KC_2G4) {
        return true;
    }

    keycode_shadow = keycode;

#    ifndef WLS_KEYCODE_PAIR_TIME
#        define WLS_KEYCODE_PAIR_TIME 3000
#    endif

#    define WLS_KEYCODE_EXEC(wls_dev)                                                                                          \
        do {                                                                                                                   \
            if (record->event.pressed) {                                                                                       \
                if (wireless_get_current_devs() != wls_dev) wireless_devs_change(wireless_get_current_devs(), wls_dev, false); \
                if (wls_process_long_press_token == INVALID_DEFERRED_TOKEN) {                                                  \
                    wls_process_long_press_token = defer_exec(WLS_KEYCODE_PAIR_TIME, wls_process_long_press, &keycode_shadow); \
                }                                                                                                              \
            } else {                                                                                                           \
                cancel_deferred_exec(wls_process_long_press_token);                                                            \
                wls_process_long_press_token = INVALID_DEFERRED_TOKEN;                                                         \
            }                                                                                                                  \
        } while (false)

    // Cache mode detection result to avoid repeated calls
    static uint8_t cached_mode = 0;
    static uint32_t last_mode_check = 0;
    uint32_t current_time = timer_read32();
    
    if (timer_elapsed32(last_mode_check) > 100) { // Only check mode every 100ms
        cached_mode = confinfo.devs;
        hs_modeio_detection(true, &cached_mode, confinfo.last_btdevs);
        last_mode_check = current_time;
    }

    switch (keycode) {
        case KC_BT1: {
            if ((cached_mode == hs_bt) || (cached_mode == hs_wireless) || (cached_mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_BT1);
                hs_rgb_blink_set_timer(timer_read32());
            }
        } break;
        case KC_BT2: {
            if ((cached_mode == hs_bt) || (cached_mode == hs_wireless) || (cached_mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_BT2);
                hs_rgb_blink_set_timer(timer_read32());
            }
        } break;
        case KC_BT3: {
            if ((cached_mode == hs_bt) || (cached_mode == hs_wireless) || (cached_mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_BT3);
                hs_rgb_blink_set_timer(timer_read32());
            }
        } break;
        case KC_2G4: {
            if ((cached_mode == hs_2g4) || (cached_mode == hs_wireless) || (cached_mode == hs_none)) {
                WLS_KEYCODE_EXEC(DEVS_2G4);
                hs_rgb_blink_set_timer(timer_read32());
            }
        } break;
        default:
            return true;
    }

    return false;
}
#endif

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Only process on key press, not release, to reduce overhead
    if (!record->event.pressed) {
        return true;
    }
    
    if (test_white_light_flag) {
        test_white_light_flag = false;
        rgb_matrix_set_color_all(0x00, 0x00, 0x00);
    }

    // Only update timer if connected to reduce unnecessary timer operations
    if (*md_getp_state() == MD_STATE_CONNECTED) {
        hs_rgb_blink_set_timer(timer_read32());
    }

    return true;
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (process_record_user(keycode, record) != true) {
        return false;
    }

#ifdef WIRELESS_ENABLE
    if (process_record_wls(keycode, record) != true) {
        return false;
    }
#endif
    switch (keycode) {
        case QK_BOOT: {
            if (record->event.pressed) {
                dprintf("into boot!!!\r\n");
                eeconfig_disable();
                bootloader_jump();
            }
        } break;
        case EE_CLR: {
            if (record->event.pressed) {
                ee_clr_timer = timer_read32();
            } else {
                ee_clr_timer = 0;
            }

            return false;
        } break;
        case RGB_SPI: {
            if (record->event.pressed) {
                if (rgb_matrix_get_speed() >= (RGB_MATRIX_SPD_STEP * 5)) {
                    rgb_blink_dir();
                }
            }
        } break;
        case RGB_SPD: {
            if (record->event.pressed) {
                if (rgb_matrix_get_speed() <= RGB_MATRIX_SPD_STEP) {
                    rgb_blink_dir();
                    rgb_matrix_set_speed(RGB_MATRIX_SPD_STEP);

                    return false;
                }
            }
        } break;
        case RGB_VAI: {
            if (record->event.pressed) {
                if (rgb_matrix_get_val() >= (RGB_MATRIX_MAXIMUM_BRIGHTNESS - RGB_MATRIX_VAL_STEP)) {
                    rgb_blink_dir();
                    start_hsv.v = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
                } else {
                    start_hsv.v = rgb_matrix_get_val() + RGB_MATRIX_VAL_STEP;
                }
            }
        } break;
        case RGB_VAD: {
            if (record->event.pressed) {
                if (rgb_matrix_get_val() <= RGB_MATRIX_VAL_STEP) {
                    rgb_blink_dir();
                    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT - RGBLED_NUM; i++) {
                        rgb_matrix_set_color(i, 0, 0, 0);
                    }
                    start_hsv.v = 0;
                } else {
                    start_hsv.v = rgb_matrix_get_val() - RGB_MATRIX_VAL_STEP;
                }
            }
        } break;
        case TO(_BL): {
            if (record->event.pressed) {
                rgb_matrix_hs_set_remain_time(HS_RGB_BLINK_INDEX_MAC, 0);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_WIN, (RGB){RGB_WHITE}, 250, 3);
                if (keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_BL);
                    layer_move(0);
                }
            }

            return false;
        } break;
        case TO(_MBL): {
            if (record->event.pressed) {
                rgb_matrix_hs_set_remain_time(HS_RGB_BLINK_INDEX_WIN, 0);
                rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_MAC, (RGB){RGB_WHITE}, 250, 3);
                if (!keymap_is_mac_system()) {
                    set_single_persistent_default_layer(_MBL);
                    layer_move(0);
                }
            }

            return false;
        } break;
        case CHK_BAT: {
            extern bool charge_chk_flag;
            charge_chk_flag = record->event.pressed;
            if (record->event.pressed) {
                confinfo.chk_bat = !confinfo.chk_bat;
                eeconfig_confinfo_update(confinfo.raw);
                return false;
            }
        } break;
        case HS_BATQ: {
            extern bool rk_bat_req_flag;
            rk_bat_req_flag = (confinfo.devs != DEVS_USB) && record->event.pressed;
            return false;
        } break;
        default:
            break;
    }

    return true;
}

void query(void) {
    if (rk_bat_req_flag) {
#ifdef RGBLIGHT_ENABLE
        for (uint8_t i = 0; i < (RGB_MATRIX_LED_COUNT - RGBLED_NUM); i++) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
#else
        rgb_matrix_set_color_all(0x00, 0x00, 0x00);
#endif
        for (uint8_t i = 0; i < 10; i++) {
            uint8_t mi_index[10] = RGB_MATRIX_BAT_INDEX_MAP;
            if ((i < (*md_getp_bat() / 10)) || (i < 1)) {
                if (*md_getp_bat() >= (IM_BAT_REQ_LEVEL1_VAL)) {
                    rgb_matrix_set_color(mi_index[i], IM_BAT_REQ_LEVEL1_COLOR);
                } else if (*md_getp_bat() >= (IM_BAT_REQ_LEVEL2_VAL)) {
                    rgb_matrix_set_color(mi_index[i], IM_BAT_REQ_LEVEL2_COLOR);
                } else {
                    rgb_matrix_set_color(mi_index[i], IM_BAT_REQ_LEVEL3_COLOR);
                }
            } else {
                rgb_matrix_set_color(mi_index[i], 0x00, 0x00, 0x00);
            }
        }
    }

    if (charge_chk_flag) {
        if (charging_state) {
            if (bat_full_flag) {
                rgb_matrix_set_color(CHARGE_CHK_INDEX, IM_BAT_REQ_LEVEL1_COLOR);
            } else {
                rgb_matrix_set_color(CHARGE_CHK_INDEX, IM_BAT_REQ_LEVEL3_COLOR);
            }
        }
    }
}

void housekeeping_task_user(void) {
    uint8_t         hs_now_mode;
    static uint32_t hs_current_time;
    static bool     val_value = false;
    static bool     last_charging_state = false;
    static bool     last_bat_full_flag = false;

    charging_state = readPin(HS_BAT_CABLE_PIN);
    bat_full_flag = readPin(BAT_FULL_PIN);

    // Only process charging state changes or every 5 seconds instead of 1 second
    bool state_changed = (charging_state != last_charging_state) || (bat_full_flag != last_bat_full_flag);
    
    if (!state_changed && hs_current_time && timer_elapsed32(hs_current_time) < 5000) {
        return; // Early return to reduce processing overhead
    }

    last_charging_state = charging_state;
    last_bat_full_flag = bat_full_flag;

    if (charging_state && (bat_full_flag)) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_DONE;
    } else if (charging_state) {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING;
    } else {
        hs_now_mode = MD_SND_CMD_DEVCTRL_CHARGING_STOP;
    }

    if (!hs_current_time || timer_elapsed32(hs_current_time) > 5000 || state_changed) {
        hs_current_time = timer_read32();
        md_send_devctrl(hs_now_mode);
        md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);
    }

    if (charging_state) {
        writePin(HS_LED_BOOSTING_PIN, 0);
        if (!val_value) {
            rgb_matrix_sethsv_noeeprom(start_hsv.h, start_hsv.s, 150);
        }
        val_value = true;

    } else {
        writePin(HS_LED_BOOSTING_PIN, 1);
        if (val_value) {
            rgb_matrix_sethsv(start_hsv.h, start_hsv.s, start_hsv.v);
        }
        val_value = false;
    }
}

#ifdef RGB_MATRIX_ENABLE

#    ifdef WIRELESS_ENABLE
bool     wls_rgb_indicator_reset    = false;
uint32_t wls_rgb_indicator_timer    = 0x00;
uint32_t wls_rgb_indicator_interval = 0;
uint32_t wls_rgb_indicator_times    = 0;
uint32_t wls_rgb_indicator_index    = 0;
RGB      wls_rgb_indicator_rgb      = {0};

void rgb_matrix_wls_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    wls_rgb_indicator_timer = timer_read32();

    wls_rgb_indicator_index    = index;
    wls_rgb_indicator_interval = interval;
    wls_rgb_indicator_times    = times * 2;
    wls_rgb_indicator_rgb      = rgb;
}

void wireless_devs_change_kb(uint8_t old_devs, uint8_t new_devs, bool reset) {
    wls_rgb_indicator_reset = reset;

    if (confinfo.devs != wireless_get_current_devs()) {
        confinfo.devs = wireless_get_current_devs();
        if (confinfo.devs > 0 && confinfo.devs < 4) confinfo.last_btdevs = confinfo.devs;
        eeconfig_confinfo_update(confinfo.raw);
    }

    switch (new_devs) {
        case DEVS_BT1: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_LBACK_COLOR_BT1}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT1, (RGB){HS_PAIR_COLOR_BT1}, 500, 1);
            }
        } break;
        case DEVS_BT2: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_LBACK_COLOR_BT2}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT2, (RGB){HS_PAIR_COLOR_BT2}, 500, 1);
            }
        } break;
        case DEVS_BT3: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_LBACK_COLOR_BT3}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_BT3, (RGB){HS_PAIR_COLOR_BT3}, 500, 1);
            }
        } break;
        case DEVS_BT4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(41, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(41, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_BT5: {
            if (reset) {
                rgb_matrix_wls_indicator_set(42, (RGB){RGB_BLUE}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(42, (RGB){RGB_BLUE}, 500, 1);
            }
        } break;
        case DEVS_2G4: {
            if (reset) {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_LBACK_COLOR_2G4}, 200, 1);
            } else {
                rgb_matrix_wls_indicator_set(HS_RGB_BLINK_INDEX_2G4, (RGB){HS_PAIR_COLOR_2G4}, 500, 1);
            }
        } break;
        default:
            break;
    }
}

bool rgb_matrix_wls_indicator_cb(void) {
    if (*md_getp_state() != MD_STATE_CONNECTED) {
        wireless_devs_change_kb(wireless_get_current_devs(), wireless_get_current_devs(), wls_rgb_indicator_reset);
        return true;
    }

    // refresh led
    led_wakeup();

    return false;
}

void rgb_matrix_wls_indicator(void) {
    if (wls_rgb_indicator_timer) {
        if (timer_elapsed32(wls_rgb_indicator_timer) >= wls_rgb_indicator_interval) {
            wls_rgb_indicator_timer = timer_read32();

            if (wls_rgb_indicator_times) {
                wls_rgb_indicator_times--;
            }

            if (wls_rgb_indicator_times <= 0) {
                wls_rgb_indicator_timer = 0x00;
                if (rgb_matrix_wls_indicator_cb() != true) {
                    return;
                }
            }
        }

        if (wls_rgb_indicator_times % 2) {
            rgb_matrix_set_color(wls_rgb_indicator_index, wls_rgb_indicator_rgb.r, wls_rgb_indicator_rgb.g, wls_rgb_indicator_rgb.b);
        } else {
            rgb_matrix_set_color(wls_rgb_indicator_index, 0x00, 0x00, 0x00);
        }
    }
}

void rgb_matrix_hs_bat_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (!hs_rgb_bat[i].active) {
            hs_rgb_bat[i].active   = true;
            hs_rgb_bat[i].timer    = timer_read32();
            hs_rgb_bat[i].interval = interval;
            hs_rgb_bat[i].times    = times * 2;
            hs_rgb_bat[i].index    = index;
            hs_rgb_bat[i].rgb      = rgb;
            break;
        }
    }
}

void rgb_matrix_hs_bat(void) {
    for (int i = 0; i < HS_RGB_BAT_COUNT; i++) {
        if (hs_rgb_bat[i].active) {
            if (timer_elapsed32(hs_rgb_bat[i].timer) >= hs_rgb_bat[i].interval) {
                hs_rgb_bat[i].timer = timer_read32();

                if (hs_rgb_bat[i].times) {
                    hs_rgb_bat[i].times--;
                }

                if (hs_rgb_bat[i].times <= 0) {
                    hs_rgb_bat[i].active = false;
                    hs_rgb_bat[i].timer  = 0x00;
                }
            }

            if (hs_rgb_bat[i].times % 2) {
                rgb_matrix_set_color(hs_rgb_bat[i].index, hs_rgb_bat[i].rgb.r, hs_rgb_bat[i].rgb.g, hs_rgb_bat[i].rgb.b);
            } else {
                rgb_matrix_set_color(hs_rgb_bat[i].index, 0x00, 0x00, 0x00);
            }
        }
    }
}

void bat_indicators(void) {
    static uint32_t battery_process_time = 0;
    if (charging_state && (bat_full_flag) && confinfo.chk_bat) {
        rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0, 25, 0);
        battery_process_time = 0;
    } else if (charging_state && confinfo.chk_bat) {
        battery_process_time = 0;
        rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 25, 0, 0);
    } else if (*md_getp_bat() <= BATTERY_CAPACITY_LOW) {
        rgb_matrix_hs_bat_set(HS_MATRIX_BLINK_INDEX_BAT, (RGB){0x00, 25, 0x00}, 250, 1);

        if (*md_getp_bat() <= BATTERY_CAPACITY_STOP) {
            if (!battery_process_time) {
                battery_process_time = timer_read32();
            }

            if (battery_process_time && timer_elapsed32(battery_process_time) > 60000) {
                battery_process_time = 0;
                lower_sleep          = true;
                lpwr_set_timeout_manual(true);
            }
        }
    } else {
        rgb_matrix_set_color(HS_MATRIX_BLINK_INDEX_BAT, 0, 0, 0);
        battery_process_time = 0;
    }
}

#    endif

#endif

void rgb_blink_dir(void) {
    rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_VAI, (RGB){0x20, 0x20, 0x20}, 250, 3);
    rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_VAD, (RGB){0x20, 0x20, 0x20}, 250, 3);
    rgb_matrix_hs_indicator_set(HS_RGB_BLINK_INDEX_SPI, (RGB){0x20, 0x20, 0x20}, 250, 3);
}

bool hs_reset_settings_user(void) {
    rgb_matrix_hs_indicator_set(0xFF, (RGB){0x10, 0x10, 0x10}, 250, 3);

    return true;
}

void nkr_indicators_hook(uint8_t index) {
    if ((hs_rgb_indicators[index].rgb.r == 0x6E) && (hs_rgb_indicators[index].rgb.g == 0x00) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x6E, 0x00, 0x00}, 250, 1);

    } else if ((hs_rgb_indicators[index].rgb.r == 0x00) && (hs_rgb_indicators[index].rgb.g == 0x6E) && (hs_rgb_indicators[index].rgb.b == 0x00)) {
        rgb_matrix_hs_indicator_set(0xFF, (RGB){0x00, 0x00, 0x6F}, 250, 1);
    }
}

void rgb_matrix_hs_indicator_set(uint8_t index, RGB rgb, uint32_t interval, uint8_t times) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (!hs_rgb_indicators[i].active) {
            hs_rgb_indicators[i].active   = true;
            hs_rgb_indicators[i].timer    = timer_read32();
            hs_rgb_indicators[i].interval = interval;
            hs_rgb_indicators[i].times    = times * 2;
            hs_rgb_indicators[i].index    = index;
            hs_rgb_indicators[i].rgb      = rgb;
            if (index != 0xFF)
                hs_rgb_indicators[i].blink_cb = NULL;
            else {
                hs_rgb_indicators[i].blink_cb = nkr_indicators_hook;
            }
            break;
        }
    }
}

void rgb_matrix_hs_set_remain_time(uint8_t index, uint8_t remain_time) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].index == index) {
            hs_rgb_indicators[i].times  = 0;
            hs_rgb_indicators[i].active = false;
            break;
        }
    }
}

void rgb_matrix_hs_indicator(void) {
    for (int i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        if (hs_rgb_indicators[i].active) {
            if (timer_elapsed32(hs_rgb_indicators[i].timer) >= hs_rgb_indicators[i].interval) {
                hs_rgb_indicators[i].timer = timer_read32();

                if (hs_rgb_indicators[i].times) {
                    hs_rgb_indicators[i].times--;
                }

                if (hs_rgb_indicators[i].times <= 0) {
                    hs_rgb_indicators[i].active = false;
                    hs_rgb_indicators[i].timer  = 0x00;
                    if (hs_rgb_indicators[i].blink_cb != NULL) hs_rgb_indicators[i].blink_cb(i);
                    continue;
                }
            }

            if ((hs_rgb_indicators[i].times % 2)) {
                if (hs_rgb_indicators[i].index == 0xFF) {
                    rgb_matrix_set_color_all(hs_rgb_indicators[i].rgb.r, hs_rgb_indicators[i].rgb.g, hs_rgb_indicators[i].rgb.b);
                } else {
                    rgb_matrix_set_color(hs_rgb_indicators[i].index, hs_rgb_indicators[i].rgb.r, hs_rgb_indicators[i].rgb.g, hs_rgb_indicators[i].rgb.b);
                }
            } else {
                if (hs_rgb_indicators[i].index == 0xFF) {
                    rgb_matrix_set_color_all(0x00, 0x00, 0x00);
                } else {
                    rgb_matrix_set_color(hs_rgb_indicators[i].index, 0x00, 0x00, 0x00);
                }
            }
        }
    }
}

bool rgb_matrix_indicators_advanced_kb(uint8_t led_min, uint8_t led_max) {
    static uint32_t last_indicator_update = 0;
    uint32_t current_time = timer_read32();
    
    if (test_white_light_flag) {
        RGB rgb_test_open = hsv_to_rgb((HSV){.h = 0, .s = 0, .v = RGB_MATRIX_VAL_STEP * 5});
        rgb_matrix_set_color_all(rgb_test_open.r, rgb_test_open.g, rgb_test_open.b);

        return false;
    }
#ifdef RGBLIGHT_ENABLE
    if (rgb_matrix_indicators_advanced_user(led_min, led_max) != true) {
        return false;
    }
#endif

    if (ee_clr_timer && timer_elapsed32(ee_clr_timer) > 3000) {
        hs_reset_settings();
        ee_clr_timer = 0;
    }

    // Always update caps lock and win lock indicators (these are important)
    if (host_keyboard_led_state().caps_lock)
        rgb_matrix_set_color(HS_RGB_INDEX_CAPS, 0x20, 0x20, 0x20);
    else
        rgb_matrix_set_color(HS_RGB_INDEX_CAPS, RGB_BLACK);
    if (!keymap_is_mac_system() && keymap_config.no_gui)
        rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, 0x20, 0x20, 0x20);
    else
        rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, RGB_BLACK);

    // Reduce frequency of heavy RGB operations to every 50ms instead of every frame
    if (timer_elapsed32(last_indicator_update) >= 50) {
        last_indicator_update = current_time;
        
#ifdef WIRELESS_ENABLE
        rgb_matrix_wls_indicator();

        if (enable_bat_indicators && !inqbat_flag) {
            rgb_matrix_hs_bat();
            bat_indicators();
            bat_indicator_cnt = timer_read32();
        }

        if (!enable_bat_indicators) {
            if (timer_elapsed32(bat_indicator_cnt) > 2000) {
                enable_bat_indicators = true;
                bat_indicator_cnt     = timer_read32();
            }
        }
#endif

        rgb_matrix_hs_indicator();
        query();
    }

    return true;
}

void hs_reset_settings(void) {
    enable_bat_indicators = false;
    eeconfig_init();
    eeconfig_update_rgb_matrix_default();

    keymap_config.raw = eeconfig_read_keymap();

#if defined(NKRO_ENABLE) && defined(FORCE_NKRO)
    keymap_config.nkro = 0;
    eeconfig_update_keymap(keymap_config.raw);
#endif

    // #if defined(WIRELESS_ENABLE)
    //     wireless_devs_change(wireless_get_current_devs(), DEVS_USB, false);
    // #endif

    if (hs_reset_settings_user() != true) {
        return;
    }
    hs_rgb_blink_set_timer(timer_read32());
    keyboard_post_init_kb();
}

void lpwr_wakeup_hook(void) {
    hs_mode_scan(false, confinfo.devs, confinfo.last_btdevs);

    gpio_write_pin_high(LED_POWER_EN_PIN);
    gpio_write_pin_high(A9);
    gpio_write_pin_high(HS_LED_BOOSTING_PIN);
}

void bootmagic_scan(void) {
    matrix_scan();
    wait_ms(DEBOUNCE * 2);
    matrix_scan();

    // esc + space
    if ((matrix_get_row(0) & (1 << 0)) && matrix_get_row(5) & (1 << 5)) {
        // Jump to bootloader.
        bootloader_jump();
    }
}
