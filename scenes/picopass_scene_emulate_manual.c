#include "../picopass_i.h"
#include "../picopass_keys.h"
#include <dolphin/dolphin.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "PicopassSceneEmulateManual"

typedef enum {
    PicopassManualFieldNone = 0,
    PicopassManualFieldFacility,
    PicopassManualFieldCard,
} PicopassManualField;

typedef struct {
    WiegandFormat format;
    PicopassManualField pending_field;
    uint32_t facility_code;
    uint64_t card_number;
} PicopassManualState;

enum ManualMenuIndex {
    ManualMenuFormat,
    ManualMenuFacility,
    ManualMenuCard,
    ManualMenuEmulate,
};

static PicopassManualState manual_state;

static const char* picopass_emulate_manual_format_name(WiegandFormat format) {
    switch(format) {
    case WiegandFormat_H10301:
        return "H10301";
    case WiegandFormat_C1k35s:
        return "C1k35s";
    case WiegandFormat_H10302:
        return "H10302";
    case WiegandFormat_H10304:
        return "H10304";
    default:
        return "Unknown";
    }
}

static uint32_t picopass_emulate_manual_max_facility(WiegandFormat format) {
    switch(format) {
    case WiegandFormat_H10301:
        return 0xFF;
    case WiegandFormat_C1k35s:
        return 0x7FF;
    case WiegandFormat_H10304:
        return 0xFFFF;
    default:
        return 0;
    }
}

static uint64_t picopass_emulate_manual_max_card(WiegandFormat format) {
    switch(format) {
    case WiegandFormat_H10301:
        return 0xFFFF;
    case WiegandFormat_C1k35s:
        return 0xFFFFF;
    case WiegandFormat_H10302:
        return (1ULL << 35) - 1;
    case WiegandFormat_H10304:
        return 0x7FFFF;
    default:
        return 0xFFFFFFFFULL;
    }
}

static uint64_t picopass_emulate_manual_clamp(uint64_t value, uint64_t max) {
    return (value > max) ? max : value;
}

static void picopass_emulate_manual_seed_legacy(PicopassDeviceData* dev_data) {
    picopass_device_data_clear(dev_data);
    dev_data->auth = PicopassDeviceAuthMethodKey;

    static const uint8_t default_csn[PICOPASS_BLOCK_LEN] =
        {0x6D, 0xC2, 0x5B, 0x15, 0xFE, 0xFF, 0x12, 0xE0};
    static const uint8_t default_config[PICOPASS_BLOCK_LEN] =
        {0x12, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0xFF, (PICOPASS_FUSE_CRYPT10 | 0x24)};
    static const uint8_t default_epurse[PICOPASS_BLOCK_LEN] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0x05, 0xFE, 0xFF, 0xFF};
    static const uint8_t default_aia[PICOPASS_BLOCK_LEN] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t default_pacs_cfg[PICOPASS_BLOCK_LEN] =
        {0x03, 0x03, 0x03, 0x03, 0x00, 0x03, 0xE0, 0x17};

    memcpy(dev_data->card_data[PICOPASS_CSN_BLOCK_INDEX].data, default_csn, PICOPASS_BLOCK_LEN);
    dev_data->card_data[PICOPASS_CSN_BLOCK_INDEX].valid = true;

    memcpy(
        dev_data->card_data[PICOPASS_CONFIG_BLOCK_INDEX].data,
        default_config,
        PICOPASS_BLOCK_LEN);
    dev_data->card_data[PICOPASS_CONFIG_BLOCK_INDEX].valid = true;

    memcpy(
        dev_data->card_data[PICOPASS_SECURE_EPURSE_BLOCK_INDEX].data,
        default_epurse,
        PICOPASS_BLOCK_LEN);
    dev_data->card_data[PICOPASS_SECURE_EPURSE_BLOCK_INDEX].valid = true;

    dev_data->card_data[PICOPASS_SECURE_KD_BLOCK_INDEX].valid = true;
    dev_data->card_data[PICOPASS_SECURE_KC_BLOCK_INDEX].valid = true;

    memcpy(
        dev_data->card_data[PICOPASS_SECURE_AIA_BLOCK_INDEX].data, default_aia, PICOPASS_BLOCK_LEN);
    dev_data->card_data[PICOPASS_SECURE_AIA_BLOCK_INDEX].valid = true;

    memcpy(
        dev_data->card_data[PICOPASS_ICLASS_PACS_CFG_BLOCK_INDEX].data,
        default_pacs_cfg,
        PICOPASS_BLOCK_LEN);
    dev_data->card_data[PICOPASS_ICLASS_PACS_CFG_BLOCK_INDEX].valid = true;

    dev_data->card_data[7].valid = true;
    dev_data->card_data[8].valid = true;
    dev_data->card_data[9].valid = true;
}

static bool picopass_emulate_manual_build(Picopass* picopass) {
    PicopassDeviceData* dev_data = &picopass->dev->dev_data;
    PicopassPacs* pacs = &dev_data->pacs;

    picopass_emulate_manual_seed_legacy(dev_data);

    pacs->legacy = true;
    pacs->se_enabled = false;
    pacs->sio = false;
    pacs->biometrics = 0;
    pacs->pin_length = 0;
    pacs->encryption = PicopassDeviceEncryption3DES;
    pacs->elite_kdf = false;
    memcpy(pacs->key, picopass_iclass_key, PICOPASS_BLOCK_LEN);
    memset(pacs->pin0, 0, PICOPASS_BLOCK_LEN);
    memset(pacs->pin1, 0, PICOPASS_BLOCK_LEN);

    if(manual_state.format == WiegandFormat_H10302) {
        manual_state.facility_code = 0;
    }

    wiegand_card_t card = {
        .FacilityCode = manual_state.facility_code,
        .CardNumber = manual_state.card_number,
        .ParityValid = true,
    };
    wiegand_message_t packed = {};
    bool packed_ok = false;

    switch(manual_state.format) {
    case WiegandFormat_H10301:
        packed_ok = picopass_Pack_H10301(&card, &packed);
        break;
    case WiegandFormat_C1k35s:
        packed_ok = picopass_Pack_C1k35s(&card, &packed);
        break;
    case WiegandFormat_H10302:
        packed_ok = picopass_Pack_H10302(&card, &packed);
        break;
    case WiegandFormat_H10304:
        packed_ok = picopass_Pack_H10304(&card, &packed);
        break;
    default:
        break;
    }

    if(!packed_ok) {
        FURI_LOG_E(TAG, "Failed to pack credential, format: %d", manual_state.format);
        return false;
    }

    pacs->bitLength = packed.Length;
    picopass_pacs_load_from_wmo(pacs, &packed);
    picopass_device_build_credential(pacs, dev_data->card_data);

    return true;
}

static void picopass_scene_emulate_manual_submenu_callback(void* context, uint32_t index) {
    Picopass* picopass = context;
    view_dispatcher_send_custom_event(picopass->view_dispatcher, index);
}

static void picopass_scene_emulate_manual_update_menu(Picopass* picopass) {
    Submenu* submenu = picopass->submenu;
    submenu_reset(submenu);

    char label[32];
    snprintf(
        label,
        sizeof(label),
        "Format: %s",
        picopass_emulate_manual_format_name(manual_state.format));
    submenu_add_item(
        submenu,
        label,
        ManualMenuFormat,
        picopass_scene_emulate_manual_submenu_callback,
        picopass);

    if(manual_state.format == WiegandFormat_H10302) {
        snprintf(label, sizeof(label), "Facility Code: (n/a)");
    } else {
        snprintf(label, sizeof(label), "Facility Code: %lu", (unsigned long)manual_state.facility_code);
    }
    submenu_add_item(
        submenu,
        label,
        ManualMenuFacility,
        picopass_scene_emulate_manual_submenu_callback,
        picopass);

    snprintf(label, sizeof(label), "Card Number: %llu", (unsigned long long)manual_state.card_number);
    submenu_add_item(
        submenu,
        label,
        ManualMenuCard,
        picopass_scene_emulate_manual_submenu_callback,
        picopass);

    submenu_add_item(
        submenu,
        "Start Emulation",
        ManualMenuEmulate,
        picopass_scene_emulate_manual_submenu_callback,
        picopass);

    submenu_set_selected_item(
        submenu,
        scene_manager_get_scene_state(
            picopass->scene_manager, PicopassSceneEmulateManual));
}

static void picopass_scene_emulate_manual_text_input_callback(void* context) {
    Picopass* picopass = context;
    view_dispatcher_send_custom_event(picopass->view_dispatcher, PicopassCustomEventTextInputDone);
}

static void picopass_scene_emulate_manual_start_input(
    Picopass* picopass,
    PicopassManualField field,
    const char* header,
    uint64_t value) {
    manual_state.pending_field = field;
    if(value > 0) {
        picopass_text_store_set(picopass, "%llu", (unsigned long long)value);
    } else {
        picopass_text_store_clear(picopass);
    }

    TextInput* text_input = picopass->text_input;
    text_input_set_header_text(text_input, header);
    text_input_set_result_callback(
        text_input,
        picopass_scene_emulate_manual_text_input_callback,
        picopass,
        picopass->text_store,
        sizeof(picopass->text_store),
        true);
    view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewTextInput);
}

static void picopass_scene_emulate_manual_cycle_format(void) {
    manual_state.format =
        (manual_state.format + 1) >= WiegandFormat_Count ? WiegandFormat_H10301 :
                                                           manual_state.format + 1;
    uint32_t fc_max = picopass_emulate_manual_max_facility(manual_state.format);
    if(fc_max == 0) {
        manual_state.facility_code = 0;
    } else if(manual_state.facility_code > fc_max) {
        manual_state.facility_code = fc_max;
    }

    uint64_t cn_max = picopass_emulate_manual_max_card(manual_state.format);
    if(manual_state.card_number > cn_max) {
        manual_state.card_number = cn_max;
    }
}

static void picopass_scene_emulate_manual_apply_text(Picopass* picopass) {
    char* endptr = NULL;
    uint64_t value = strtoull(picopass->text_store, &endptr, 10);
    if((endptr == picopass->text_store) || (*endptr != '\0')) {
        value = 0;
    }

    if(manual_state.pending_field == PicopassManualFieldFacility) {
        uint32_t max = picopass_emulate_manual_max_facility(manual_state.format);
        if(max > 0) {
            value = picopass_emulate_manual_clamp(value, max);
        } else {
            value = 0;
        }
        manual_state.facility_code = (uint32_t)value;
    } else if(manual_state.pending_field == PicopassManualFieldCard) {
        uint64_t max = picopass_emulate_manual_max_card(manual_state.format);
        value = picopass_emulate_manual_clamp(value, max);
        manual_state.card_number = value;
    }

    manual_state.pending_field = PicopassManualFieldNone;
}

void picopass_scene_emulate_manual_on_enter(void* context) {
    Picopass* picopass = context;

    manual_state.format = WiegandFormat_H10301;
    manual_state.facility_code = 0;
    manual_state.card_number = 0;
    manual_state.pending_field = PicopassManualFieldNone;

    picopass_scene_emulate_manual_update_menu(picopass);
    view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewMenu);
}

bool picopass_scene_emulate_manual_on_event(void* context, SceneManagerEvent event) {
    Picopass* picopass = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ManualMenuFormat) {
            scene_manager_set_scene_state(
                picopass->scene_manager, PicopassSceneEmulateManual, ManualMenuFormat);
            picopass_scene_emulate_manual_cycle_format();
            picopass_scene_emulate_manual_update_menu(picopass);
            consumed = true;
        } else if(event.event == ManualMenuFacility) {
            scene_manager_set_scene_state(
                picopass->scene_manager, PicopassSceneEmulateManual, ManualMenuFacility);
            picopass_scene_emulate_manual_start_input(
                picopass, PicopassManualFieldFacility, "Facility Code (dec)", manual_state.facility_code);
            consumed = true;
        } else if(event.event == ManualMenuCard) {
            scene_manager_set_scene_state(
                picopass->scene_manager, PicopassSceneEmulateManual, ManualMenuCard);
            picopass_scene_emulate_manual_start_input(
                picopass, PicopassManualFieldCard, "Card Number (dec)", manual_state.card_number);
            consumed = true;
        } else if(event.event == ManualMenuEmulate) {
            scene_manager_set_scene_state(
                picopass->scene_manager, PicopassSceneEmulateManual, ManualMenuEmulate);
            if(picopass_emulate_manual_build(picopass)) {
                scene_manager_next_scene(picopass->scene_manager, PicopassSceneEmulate);
            } else {
                // Stay on menu if packing failed
                picopass_scene_emulate_manual_update_menu(picopass);
            }
            consumed = true;
        } else if(event.event == PicopassCustomEventTextInputDone) {
            picopass_scene_emulate_manual_apply_text(picopass);
            picopass_scene_emulate_manual_update_menu(picopass);
            view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewMenu);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(picopass->scene_manager);
    }

    return consumed;
}

void picopass_scene_emulate_manual_on_exit(void* context) {
    Picopass* picopass = context;

    // Clear views
    text_input_set_result_callback(picopass->text_input, NULL, NULL, NULL, 0, false);
    text_input_set_header_text(picopass->text_input, "");
    text_input_reset(picopass->text_input);
    submenu_reset(picopass->submenu);
}
