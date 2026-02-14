#include "../picopass_i.h"
#include "../picopass_wiegand.h"
#include <dolphin/dolphin.h>

#define TAG "PicopassSceneEmulate"

bool picopass_scene_create_get_format_hint(WiegandFormat* out_format);

static WiegandFormat format = WiegandFormat_None;

static uint8_t
    picopass_scene_emulate_get_bit_by_position(const wiegand_message_t* data, uint8_t pos) {
    if(pos >= data->Length) return 0;
    pos = (data->Length - pos) - 1;
    if(pos > 95) {
        return 0;
    } else if(pos > 63) {
        return (data->Top >> (pos - 64)) & 1;
    } else if(pos > 31) {
        return (data->Mid >> (pos - 32)) & 1;
    } else {
        return (data->Bot >> pos) & 1;
    }
}

static uint64_t picopass_scene_emulate_get_linear_field(
    const wiegand_message_t* data,
    uint8_t first_bit,
    uint8_t length) {
    uint64_t result = 0;
    for(uint8_t i = 0; i < length; i++) {
        result = (result << 1) | picopass_scene_emulate_get_bit_by_position(data, first_bit + i);
    }
    return result;
}

static bool picopass_scene_emulate_unpack_no_parity(
    WiegandFormat hint,
    const wiegand_message_t* packed,
    wiegand_card_t* card) {
    wiegand_message_t hinted = *packed;
    memset(card, 0, sizeof(*card));

    switch(hint) {
    case WiegandFormat_H10301:
        hinted.Length = 26;
        card->CardNumber = (hinted.Bot >> 1) & 0xFFFF;
        card->FacilityCode = (hinted.Bot >> 17) & 0xFF;
        return true;
    case WiegandFormat_C1k35s:
        hinted.Length = 35;
        card->CardNumber = (hinted.Bot >> 1) & 0x000FFFFF;
        card->FacilityCode = ((hinted.Mid & 1) << 11) | (hinted.Bot >> 21);
        return true;
    case WiegandFormat_H10302:
        hinted.Length = 37;
        card->CardNumber = picopass_scene_emulate_get_linear_field(&hinted, 1, 35);
        return true;
    case WiegandFormat_H10304:
        hinted.Length = 37;
        card->FacilityCode = picopass_scene_emulate_get_linear_field(&hinted, 1, 16);
        card->CardNumber = picopass_scene_emulate_get_linear_field(&hinted, 17, 19);
        return true;
    default:
        return false;
    }
}

NfcCommand picopass_scene_listener_callback(PicopassListenerEvent event, void* context) {
    UNUSED(event);
    UNUSED(context);

    return NfcCommandContinue;
}

void picopass_scene_emulate_widget_callback(GuiButtonType result, InputType type, void* context) {
    Picopass* picopass = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(picopass->view_dispatcher, result);
    }
}

void picopass_scene_emulate_start(Picopass* picopass) {
    PicopassDevice* dev = picopass->dev;
    PicopassDeviceData* dev_data = &dev->dev_data;

    picopass_blink_emulate_start(picopass);

    picopass->listener = picopass_listener_alloc(picopass->nfc, dev_data);
    picopass_listener_start(picopass->listener, picopass_scene_listener_callback, picopass);
}

void picopass_scene_emulate_stop(Picopass* picopass) {
    picopass_blink_stop(picopass);
    picopass_listener_stop(picopass->listener);
    picopass_listener_free(picopass->listener);
    picopass->listener = NULL;
}

void picopass_scene_emulate_update_ui(void* context) {
    Picopass* picopass = context;
    PicopassDevice* dev = picopass->dev;
    PicopassDeviceData* dev_data = &dev->dev_data;
    PicopassPacs* pacs = &dev_data->pacs;

    Widget* widget = picopass->widget;
    widget_reset(widget);
    widget_add_icon_element(widget, 0, 3, &I_RFIDDolphinSend_97x61);
    widget_add_string_element(widget, 92, 25, AlignCenter, AlignTop, FontPrimary, "Emulating");

    // Reload credential data
    picopass_device_parse_credential(dev_data->card_data, pacs);

    wiegand_message_t packed = picopass_pacs_extract_wmo(pacs);
    wiegand_card_t card;
    bool hint_used = false;
    WiegandFormat hint = WiegandFormat_None;

    if(picopass_scene_create_get_format_hint(&hint)) {
        hint_used = picopass_scene_emulate_unpack_no_parity(hint, &packed, &card);
        if(hint_used) {
            format = hint;
        }
    }

    if(!hint_used) {
        if(picopass_Unpack_H10301(&packed, &card)) {
            format = WiegandFormat_H10301;
        } else if(picopass_Unpack_C1k35s(&packed, &card)) {
            format = WiegandFormat_C1k35s;
        } else if(picopass_Unpack_H10302(&packed, &card)) {
            format = WiegandFormat_H10302;
        } else if(picopass_Unpack_H10304(&packed, &card)) {
            format = WiegandFormat_H10304;
        } else {
            format = WiegandFormat_None;
        }
    }

    if(format == WiegandFormat_None) {
        widget_add_string_element(widget, 92, 40, AlignCenter, AlignTop, FontPrimary, "PicoPass");
        widget_add_string_element(
            widget, 34, 55, AlignLeft, AlignTop, FontSecondary, "Touch flipper to reader");
    } else {
        FuriString* desc = furi_string_alloc();

        furi_string_printf(desc, "FC:%lu", card.FacilityCode);
        widget_add_string_element(
            widget, 92, 35, AlignCenter, AlignTop, FontSecondary, furi_string_get_cstr(desc));

        furi_string_printf(desc, "CN:%llu", card.CardNumber);
        widget_add_string_element(
            widget, 92, 50, AlignCenter, AlignBottom, FontSecondary, furi_string_get_cstr(desc));
        furi_string_free(desc);

        widget_add_button_element(
            widget, GuiButtonTypeRight, "+1", picopass_scene_emulate_widget_callback, context);
        widget_add_button_element(
            widget, GuiButtonTypeLeft, "-1", picopass_scene_emulate_widget_callback, context);
        widget_add_button_element(
            widget, GuiButtonTypeCenter, "0", picopass_scene_emulate_widget_callback, context);
    }
}

void picopass_scene_emulate_on_enter(void* context) {
    Picopass* picopass = context;

    dolphin_deed(DolphinDeedNfcEmulate);
    picopass_scene_emulate_update_ui(picopass);

    picopass_scene_emulate_start(picopass);
    view_dispatcher_switch_to_view(picopass->view_dispatcher, PicopassViewWidget);
}

void picopass_scene_emulate_update_pacs(Picopass* picopass, int direction) {
    FURI_LOG_D(TAG, "Updating credential, direction: %d", direction);
    PicopassDevice* dev = picopass->dev;
    PicopassDeviceData* dev_data = &dev->dev_data;
    PicopassPacs* pacs = &dev_data->pacs;

    picopass_scene_emulate_stop(picopass);

    // Reload credential data
    picopass_device_parse_credential(dev_data->card_data, pacs);

    wiegand_message_t packed = picopass_pacs_extract_wmo(pacs);
    wiegand_card_t card;

    bool unpack_ok = false;
    if(format == WiegandFormat_H10301) {
        unpack_ok = picopass_Unpack_H10301(&packed, &card);
    } else if(format == WiegandFormat_C1k35s) {
        unpack_ok = picopass_Unpack_C1k35s(&packed, &card);
    } else if(format == WiegandFormat_H10302) {
        unpack_ok = picopass_Unpack_H10302(&packed, &card);
    } else if(format == WiegandFormat_H10304) {
        unpack_ok = picopass_Unpack_H10304(&packed, &card);
    }

    if(!unpack_ok) {
        unpack_ok = picopass_scene_emulate_unpack_no_parity(format, &packed, &card);
    }

    if(!unpack_ok) {
        format = WiegandFormat_None;
    }

    if(format != WiegandFormat_None) {
        FURI_LOG_D(
            TAG,
            "Current: FC:%lu CN:%llu ParityValid: %u",
            card.FacilityCode,
            card.CardNumber,
            card.ParityValid);
        switch(direction) {
        case -1: // Decrease
            card.CardNumber = (card.CardNumber == 0) ? 0 : card.CardNumber - 1;
            break;
        case 1: // Increase
            card.CardNumber = (card.CardNumber == 255) ? 255 : card.CardNumber + 1;
            break;
        case 0: // Zero out
            card.CardNumber = 0;
            break;
        default:
            break;
        }
        FURI_LOG_D(
            TAG,
            "Updated: FC:%lu CN:%llu ParityValid: %u",
            card.FacilityCode,
            card.CardNumber,
            card.ParityValid);

        if(format == WiegandFormat_H10301) {
            picopass_Pack_H10301(&card, &packed);
        } else if(format == WiegandFormat_C1k35s) {
            picopass_Pack_C1k35s(&card, &packed);
        } else if(format == WiegandFormat_H10302) {
            picopass_Pack_H10302(&card, &packed);
        } else if(format == WiegandFormat_H10304) {
            picopass_Pack_H10304(&card, &packed);
        } else {
            FURI_LOG_E(TAG, "Unknown format, cannot repack");
        }

        picopass_pacs_load_from_wmo(pacs, &packed);
        picopass_device_build_credential(pacs, dev_data->card_data);
    } else {
        FURI_LOG_E(TAG, "Failed to unpack credential (tried H10301, C1k35s, H10302, H10304)");
    }

    picopass_scene_emulate_update_ui(picopass);
    picopass_scene_emulate_start(picopass);
}

bool picopass_scene_emulate_on_event(void* context, SceneManagerEvent event) {
    Picopass* picopass = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PicopassCustomEventWorkerExit) {
            consumed = true;
        } else if(event.event == PicopassCustomEventNrMacSaved) {
            scene_manager_next_scene(picopass->scene_manager, PicopassSceneNrMacSaved);
            consumed = true;
        } else if(event.event == GuiButtonTypeRight) {
            picopass_scene_emulate_update_pacs(picopass, 1);
            consumed = true;
        } else if(event.event == GuiButtonTypeLeft) {
            picopass_scene_emulate_update_pacs(picopass, -1);
            consumed = true;
        } else if(event.event == GuiButtonTypeCenter) {
            picopass_scene_emulate_update_pacs(picopass, 0);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        consumed = scene_manager_previous_scene(picopass->scene_manager);
    }
    return consumed;
}

void picopass_scene_emulate_on_exit(void* context) {
    Picopass* picopass = context;

    picopass_scene_emulate_stop(picopass);

    // Clear view
    widget_reset(picopass->widget);
}
