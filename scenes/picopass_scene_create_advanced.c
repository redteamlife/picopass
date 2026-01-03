#include "../picopass_i.h"
#include "../picopass_keys.h"
#include <dolphin/dolphin.h>

#define TAG "PicopassSceneCreateAdvanced"

enum CreateAdvancedMenuIndex {
    CreateAdvancedLegacy,
    CreateAdvancedSeEnabled,
    CreateAdvancedSio,
    CreateAdvancedBiometrics,
    CreateAdvancedEncryption,
    CreateAdvancedCsn,
};

static const char* picopass_create_encryption_name(PicopassEncryption enc) {
    switch(enc) {
    case PicopassDeviceEncryptionNone:
        return "None";
    case PicopassDeviceEncryptionDES:
        return "DES";
    case PicopassDeviceEncryption3DES:
        return "3DES";
    default:
        return "Unknown";
    }
}

static void picopass_scene_create_advanced_submenu_callback(void* context, uint32_t index) {
    Picopass* picopass = context;
    view_dispatcher_send_custom_event(picopass->view_dispatcher, index);
}

static void picopass_scene_create_advanced_update_menu(Picopass* picopass) {
    Submenu* submenu = picopass->submenu;
    submenu_reset(submenu);

    char label[32];
    snprintf(label, sizeof(label), "Legacy: %s", picopass->dev->dev_data.pacs.legacy ? "On" : "Off");
    submenu_add_item(
        submenu,
        label,
        CreateAdvancedLegacy,
        picopass_scene_create_advanced_submenu_callback,
        picopass);

    snprintf(label, sizeof(label), "SE Enabled: %s", picopass->dev->dev_data.pacs.se_enabled ? "On" : "Off");
    submenu_add_item(
        submenu,
        label,
        CreateAdvancedSeEnabled,
        picopass_scene_create_advanced_submenu_callback,
        picopass);

    snprintf(label, sizeof(label), "SIO: %s", picopass->dev->dev_data.pacs.sio ? "On" : "Off");
    submenu_add_item(
        submenu,
        label,
        CreateAdvancedSio,
        picopass_scene_create_advanced_submenu_callback,
        picopass);

    snprintf(label, sizeof(label), "Biometrics: %u", picopass->dev->dev_data.pacs.biometrics);
    submenu_add_item(
        submenu,
        label,
        CreateAdvancedBiometrics,
        picopass_scene_create_advanced_submenu_callback,
        picopass);

    snprintf(
        label, sizeof(label), "Encryption: %s", picopass_create_encryption_name(picopass->dev->dev_data.pacs.encryption));
    submenu_add_item(
        submenu,
        label,
        CreateAdvancedEncryption,
        picopass_scene_create_advanced_submenu_callback,
        picopass);

    submenu_add_item(
        submenu,
        "Set CSN",
        CreateAdvancedCsn,
        picopass_scene_create_advanced_submenu_callback,
        picopass);

    submenu_set_selected_item(
        submenu,
        scene_manager_get_scene_state(picopass->scene_manager, PicopassSceneCreateAdvanced));
}

static void picopass_scene_create_advanced_text_input_done(void* context) {
    Picopass* picopass = context;
    view_dispatcher_send_custom_event(picopass->view_dispatcher, PicopassCustomEventTextInputDone);
}

static void picopass_scene_create_advanced_byte_input_done(void* context) {
    Picopass* picopass = context;
    view_dispatcher_send_custom_event(picopass->view_dispatcher, PicopassCustomEventByteInputDone);
}

void picopass_scene_create_advanced_on_enter(void* context) {
    Picopass* picopass = context;
    // Sync create state from current device data so edits reflect live values
    picopass_scene_create_advanced_update_menu(picopass);
    picopass_scene_create_advanced_update_menu(picopass);
    view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewMenu);
}

bool picopass_scene_create_advanced_on_event(void* context, SceneManagerEvent event) {
    Picopass* picopass = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == CreateAdvancedLegacy) {
            picopass->dev->dev_data.pacs.legacy = !picopass->dev->dev_data.pacs.legacy;
            picopass_scene_create_advanced_update_menu(picopass);
            consumed = true;
        } else if(event.event == CreateAdvancedSeEnabled) {
            picopass->dev->dev_data.pacs.se_enabled = !picopass->dev->dev_data.pacs.se_enabled;
            picopass_scene_create_advanced_update_menu(picopass);
            consumed = true;
        } else if(event.event == CreateAdvancedSio) {
            picopass->dev->dev_data.pacs.sio = !picopass->dev->dev_data.pacs.sio;
            picopass_scene_create_advanced_update_menu(picopass);
            consumed = true;
        } else if(event.event == CreateAdvancedBiometrics) {
            text_input_set_header_text(picopass->text_input, "Biometrics (0-255)");
            picopass_text_store_clear(picopass);
            text_input_set_result_callback(
                picopass->text_input,
                picopass_scene_create_advanced_text_input_done,
                picopass,
                picopass->text_store,
                sizeof(picopass->text_store),
                true);
            view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewTextInput);
            consumed = true;
        } else if(event.event == CreateAdvancedEncryption) {
            if(picopass->dev->dev_data.pacs.encryption == PicopassDeviceEncryptionNone) {
                picopass->dev->dev_data.pacs.encryption = PicopassDeviceEncryption3DES;
            } else {
                picopass->dev->dev_data.pacs.encryption = PicopassDeviceEncryptionNone;
            }
            picopass_scene_create_advanced_update_menu(picopass);
            consumed = true;
        } else if(event.event == CreateAdvancedCsn) {
            byte_input_set_header_text(picopass->byte_input, "Enter CSN (8 bytes)");
            byte_input_set_result_callback(
                picopass->byte_input,
                picopass_scene_create_advanced_byte_input_done,
                NULL,
                picopass,
                picopass->byte_input_store,
                PICOPASS_BLOCK_LEN);
            view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewByteInput);
            consumed = true;
        } else if(event.event == PicopassCustomEventTextInputDone) {
            uint32_t val = strtoul(picopass->text_store, NULL, 10);
            if(val > 255) val = 255;
            picopass->dev->dev_data.pacs.biometrics = (uint8_t)val;
            picopass_scene_create_advanced_update_menu(picopass);
            view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewMenu);
            consumed = true;
        } else if(event.event == PicopassCustomEventByteInputDone) {
            memcpy(
                picopass->dev->dev_data.card_data[PICOPASS_CSN_BLOCK_INDEX].data,
                picopass->byte_input_store,
                PICOPASS_BLOCK_LEN);
            picopass->dev->dev_data.card_data[PICOPASS_CSN_BLOCK_INDEX].valid = true;
            picopass_scene_create_advanced_update_menu(picopass);
            view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewMenu);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(picopass->scene_manager);
    }

    return consumed;
}

void picopass_scene_create_advanced_on_exit(void* context) {
    Picopass* picopass = context;
    // Clear views
    text_input_set_result_callback(picopass->text_input, NULL, NULL, NULL, 0, false);
    byte_input_set_result_callback(picopass->byte_input, NULL, NULL, NULL, NULL, 0);
    submenu_reset(picopass->submenu);
}
