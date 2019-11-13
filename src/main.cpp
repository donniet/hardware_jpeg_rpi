#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>

#include <bcm_host.h>
#include <ilclient.h>

#include <stdexcept>
#include <strstream>
#include <string>
#include <mutex>

const char *err2str(int err);

// Dunno where this is originally stolen from...
#define OMX_INIT_STRUCTURE(a) \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
    (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
    (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
    (a).nVersion.s.nStep = OMX_VERSION_STEP

class Component {
protected:
    OMX_HANDLETYPE handle;
    VCOS_EVENT_FLAGS_T flags;
    OMX_STRING name;
    static const char * name_prefix;

    void set_config(OMX_INDEXTYPE index, void * dat) {
        OMX_ERRORTYPE e = OMX_SetConfig(handle, index, dat);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SetConfig: %s\n", err2str(e));
            exit(1);
        }
    }

    void get_config(OMX_INDEXTYPE index, void * dat) {
        OMX_ERRORTYPE e = OMX_GetConfig(handle, index, dat);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_GetConfig: %s\n", err2str(e));
            exit(1);
        }
    }

    void set_parameter(OMX_INDEXTYPE index, void * dat) {
        OMX_ERRORTYPE e = OMX_SetParameter(handle, index, dat);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SetParmaeter: %s\n", err2str(e));
            exit(1);
        }
    }

    void get_parameter(OMX_INDEXTYPE index, void * dat) {
        OMX_ERRORTYPE e = OMX_GetParameter(handle, index, dat);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_GetParmaeter: %s\n", err2str(e));
            exit(1);
        }
    }

    static OMX_ERRORTYPE fill_buffer_done(
        OMX_IN OMX_HANDLETYPE comp,
        OMX_IN OMX_PTR app_data,
        OMX_IN OMX_BUFFERHEADERTYPE* buffer
    ) {
        Component * c = reinterpret_cast<Component*>(app_data);
        return c->fill_buffer_done_(comp, buffer);
    }

    OMX_ERRORTYPE fill_buffer_done_(
        OMX_IN OMX_HANDLETYPE comp,
        OMX_IN OMX_BUFFERHEADERTYPE* buffer
    ) {
        wake(EVENT_FILL_BUFFER_DONE);
        return OMX_ErrorNone;
    }

    static OMX_ERRORTYPE event_handler(
        OMX_IN OMX_HANDLETYPE comp,
        OMX_IN OMX_PTR app_data,
        OMX_IN OMX_EVENTTYPE event,
        OMX_IN OMX_U32 data1,
        OMX_IN OMX_U32 data2,
        OMX_IN OMX_PTR event_data)
    {
        Component * c = reinterpret_cast<Component*>(app_data);
        return c->event_handler_(comp, event, data1, data2, event_data);
    }

    OMX_ERRORTYPE event_handler_(
        OMX_IN OMX_HANDLETYPE comp,
        OMX_IN OMX_EVENTTYPE event,
        OMX_IN OMX_U32 data1,
        OMX_IN OMX_U32 data2,
        OMX_IN OMX_PTR event_data)
    {
        switch(event) {
        case OMX_EventCmdComplete:
            switch(data1) {
            case OMX_CommandStateSet:
                wake(EVENT_STATE_SET);
                break;
            case OMX_CommandPortDisable:
                wake(EVENT_PORT_DISABLE);
                break;
            case OMX_CommandPortEnable:
                wake(EVENT_PORT_ENABLE);
                break;
            case OMX_CommandFlush:
                wake(EVENT_FLUSH);
                break;
            case OMX_CommandMarkBuffer:
                wake(EVENT_MARK_BUFFER);
                break;
            }
            break;
        case OMX_EventError:
            wake(EVENT_ERROR);
            break;
        case OMX_EventMark:
            wake(EVENT_MARK);
            break;
        case OMX_EventPortSettingsChanged:
            wake(EVENT_PORT_SETTINGS_CHANGED);
            break;
        case OMX_EventParamOrConfigChanged:
            wake(EVENT_PARAM_OR_CONFIG_CHANGED);
            break;
        case OMX_EventBufferFlag:
            wake(EVENT_BUFFER_FLAG);
            break;
        case OMX_EventResourcesAcquired:
            wake(EVENT_RESOURCES_ACQUIRED);
            break;
        case OMX_EventDynamicResourcesAvailable:
            wake(EVENT_DYNAMIC_RESOURCES_AVAILABLE);
            break;
        default:
            // TODO: add logging
            break;
        }

        return OMX_ErrorNone;
    }
    void wake(VCOS_UNSIGNED event) {
        vcos_event_flags_set(&flags, event, VCOS_OR);
    }
    void wait(VCOS_UNSIGNED events, VCOS_UNSIGNED* ret = nullptr) {
        VCOS_UNSIGNED set;
        if (vcos_event_flags_get(&flags, events | EVENT_ERROR, VCOS_OR_CONSUME, VCOS_SUSPEND, &set)) {
            //TODO: gracefull error handling here
            fprintf(stderr, "error: vcos_event_flags_get\n");
            exit(1);
        }
        if (set == EVENT_ERROR) {
            exit(1);
        }
        if (ret != nullptr) {
            *ret = set;
        }
    }
public:
    //Events used with vcos_event_flags_get() and vcos_event_flags_set()
    typedef enum {
        EVENT_ERROR = 0x1,
        EVENT_PORT_ENABLE = 0x2,
        EVENT_PORT_DISABLE = 0x4,
        EVENT_STATE_SET = 0x8,
        EVENT_FLUSH = 0x10,
        EVENT_MARK_BUFFER = 0x20,
        EVENT_MARK = 0x40,
        EVENT_PORT_SETTINGS_CHANGED = 0x80,
        EVENT_PARAM_OR_CONFIG_CHANGED = 0x100,
        EVENT_BUFFER_FLAG = 0x200,
        EVENT_RESOURCES_ACQUIRED = 0x400,
        EVENT_DYNAMIC_RESOURCES_AVAILABLE = 0x800,
        EVENT_FILL_BUFFER_DONE = 0x1000,
        EVENT_EMPTY_BUFFER_DONE = 0x2000,
    } component_event;

    Component(std::string const & name) {
        this->name = new char[sizeof(name_prefix) + name.size() + 1];
        sprintf(this->name, "%s%s", name_prefix, name.c_str());

        OMX_ERRORTYPE e;

        if (vcos_event_flags_create(&flags, "component")) {
            fprintf(stderr, "error: vcos_event_flags_create\n");
            exit(1);
        }

        OMX_CALLBACKTYPE callbacks;
        callbacks.EventHandler = Component::event_handler;
        callbacks.FillBufferDone = Component::fill_buffer_done;

        e = OMX_GetHandle(&handle, this->name, this, &callbacks);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_GetHandle: %s", err2str(e));
            exit(1);
        }

        static OMX_INDEXTYPE types[] = {
            OMX_IndexParamAudioInit, OMX_IndexParamVideoInit, OMX_IndexParamImageInit, OMX_IndexParamOtherInit
        };
        OMX_PORT_PARAM_TYPE ports;
        OMX_INIT_STRUCTURE(ports);
        OMX_GetParameter(handle, OMX_IndexParamVideoInit, &ports);

        for(auto type : types) {
            if(OMX_GetParameter(handle, type, &ports) == OMX_ErrorNone) {
                OMX_U32 nPortIndex;
                for(nPortIndex = ports.nStartPortNumber; nPortIndex < ports.nStartPortNumber+ports.nPorts; nPortIndex++) {
                    e = OMX_SendCommand(handle, OMX_CommandPortDisable, nPortIndex, NULL);
                    if (e != OMX_ErrorNone) {
                        throw std::logic_error("failed to disable port");
                    }
                }
                wait(EVENT_PORT_DISABLE);
            }
        }
    }

protected:
    void set_state(OMX_STATETYPE state) {
        OMX_ERRORTYPE e = OMX_SendCommand(handle, OMX_CommandStateSet, state, 0);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SendCommand: %s\n", err2str(e));
            exit(1);
        }
        wait(EVENT_STATE_SET);
    }
public:
    void set_idle() { set_state(OMX_StateIdle); }
    void set_loaded() { set_state(OMX_StateLoaded); }
    void set_executing() { set_state(OMX_StateExecuting); }
    void set_pause() { set_state(OMX_StatePause); }
    void set_wait_for_resources() { set_state(OMX_StateWaitForResources); }

protected:
    void enable_port(OMX_U32 port) {
        OMX_ERRORTYPE e = OMX_SendCommand(handle, OMX_CommandPortEnable, port, 0);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SendCommand: %s\n", err2str(e));
            exit(1);
        }
        wait(EVENT_PORT_ENABLE);
    }
    void disable_port(OMX_U32 port) {
        OMX_ERRORTYPE e = OMX_SendCommand(handle, OMX_CommandPortDisable, port, 0);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SendCommand: %s\n", err2str(e));
            exit(1);
        }
        wait(EVENT_PORT_DISABLE);
    }
public:
    ~Component() {
        vcos_event_flags_delete(&flags);

        OMX_ERRORTYPE e = OMX_FreeHandle(handle);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "erorr: OMX_FreeHandle: %s\n", err2str(e));
            exit(1);
        }

        delete [] name;

    }
}; 

const char * Component::name_prefix = "OMX.broadcom.";

class Encoder 
    : public Component
{
protected:
    static const OMX_U32 port_index = 201;
    static const char * component_name;
    OMX_BUFFERHEADERTYPE** encoder_output_buffer;
public:
    void enable() {
        enable_port(port_index);
        OMX_PARAM_PORTDEFINITIONTYPE port;
        OMX_INIT_STRUCTURE(port);
        port.nPortIndex = port_index;

        OMX_ERRORTYPE e = OMX_GetParameter(handle, OMX_IndexParamPortDefinition, &port);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_GetParameter: %s\n", err2str(e));
            exit(1);
        }

        e = OMX_AllocateBuffer(handle, encoder_output_buffer, port_index, 0, port.nBufferSize);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_AllocateBuffer: %s\n", err2str(e));
            exit(1);
        }
        
        wait(EVENT_PORT_ENABLE);
    }

    void disable() {
        OMX_ERRORTYPE e;

        disable_port(port_index);

        e = OMX_FreeBuffer(handle, port_index, *encoder_output_buffer);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_FreeBuffer: %s\n", err2str(e));
            exit(1);
        }

        wait(EVENT_PORT_DISABLE);
    }
protected:
    OMX_U32 bitrate;
    // OMX_U32 qp_b;
    OMX_U32 qp_i;
    OMX_U32 qp_p;
    OMX_VIDEO_CODINGTYPE compression_format;
    OMX_U32 idr_period;
    OMX_BOOL sei;
    OMX_U32 eede;
    OMX_U32 eede_loss_rate;
    OMX_VIDEO_AVCPROFILETYPE profile;
    OMX_BOOL inline_headers;
    // OMX_BOOL motion_vectors;
    OMX_U32 width;
    OMX_U32 height;
    OMX_U32 stride;
    OMX_U32 framerate;

    void set_encoder_settings() {
        OMX_PARAM_PORTDEFINITIONTYPE port_st;
        OMX_INIT_STRUCTURE(port_st);
        port_st.nPortIndex = port_index;
        get_parameter(OMX_IndexParamPortDefinition, &port_st);
        port_st.format.video.nFrameWidth = width;
        port_st.format.video.nFrameHeight = height;
        port_st.format.video.nStride = stride;
        port_st.format.video.xFramerate = framerate << 16;
        port_st.format.video.nBitrate = bitrate;
        port_st.format.video.eCompressionFormat = compression_format;
        set_parameter(OMX_IndexParamPortDefinition, &port_st);

        if(bitrate > 0) {
            OMX_VIDEO_PARAM_BITRATETYPE bitrate_st;
            OMX_INIT_STRUCTURE(bitrate_st);
            bitrate_st.eControlRate = OMX_Video_ControlRateVariable;
            bitrate_st.nTargetBitrate = bitrate;
            bitrate_st.nPortIndex = port_index;
            set_parameter(OMX_IndexParamVideoBitrate, &bitrate_st);
        } else {
            OMX_VIDEO_PARAM_QUANTIZATIONTYPE quantization_st;
            OMX_INIT_STRUCTURE(quantization_st);
            quantization_st.nPortIndex = port_index;
            // returns error
            // quantization_st.nQpB = qp_b;
            quantization_st.nQpI = qp_i;
            quantization_st.nQpP = qp_p;
            set_parameter(OMX_IndexParamVideoQuantization, &quantization_st);
        }

        OMX_VIDEO_PARAM_PORTFORMATTYPE format_st;
        OMX_INIT_STRUCTURE(format_st);
        format_st.nPortIndex = port_index;
        format_st.eCompressionFormat = compression_format;
        set_parameter(OMX_IndexParamVideoPortFormat, &format_st);

        OMX_VIDEO_CONFIG_AVCINTRAPERIOD idr_st;
        OMX_INIT_STRUCTURE(idr_st);
        idr_st.nPortIndex = port_index;
        get_config(OMX_IndexConfigVideoAVCIntraPeriod, &idr_st);
        idr_st.nIDRPeriod = idr_period;
        set_config(OMX_IndexConfigVideoAVCIntraPeriod, &idr_st);

        OMX_PARAM_BRCMVIDEOAVCSEIENABLETYPE sei_st;
        OMX_INIT_STRUCTURE(sei_st);
        sei_st.nPortIndex = port_index;
        sei_st.bEnable = sei;
        set_parameter(OMX_IndexParamBrcmVideoAVCSEIEnable, &sei_st);

        OMX_VIDEO_EEDE_ENABLE eede_st;
        OMX_INIT_STRUCTURE(eede_st);
        eede_st.nPortIndex = port_index;
        eede_st.enable = eede;
        set_parameter(OMX_IndexParamBrcmEEDEEnable, &eede_st);

        OMX_VIDEO_EEDE_LOSSRATE eede_loss_rate_st;
        OMX_INIT_STRUCTURE(eede_loss_rate_st);
        eede_loss_rate_st.nPortIndex = port_index;
        eede_loss_rate_st.loss_rate = eede_loss_rate;
        set_parameter(OMX_IndexParamBrcmEEDELossRate, &eede_loss_rate_st);

        OMX_VIDEO_PARAM_AVCTYPE avc_st;
        OMX_INIT_STRUCTURE(avc_st);
        avc_st.nPortIndex = port_index;
        get_parameter(OMX_IndexParamVideoAvc, &avc_st);
        avc_st.eProfile = profile;
        set_parameter(OMX_IndexParamVideoAvc, &avc_st);

        OMX_CONFIG_PORTBOOLEANTYPE headers_st;
        OMX_INIT_STRUCTURE(headers_st);
        headers_st.nPortIndex = port_index;
        headers_st.bEnabled = inline_headers;
        set_parameter(OMX_IndexParamBrcmVideoAVCInlineHeaderEnable, &headers_st);
    }
public:
    Encoder() :
        Component(component_name), bitrate(17000000), qp_i(0), qp_p(0), 
        compression_format(OMX_VIDEO_CodingAVC), idr_period(0),
        sei(OMX_FALSE), eede(OMX_FALSE), eede_loss_rate(0),
        profile(OMX_VIDEO_AVCProfileHigh), inline_headers(OMX_FALSE),
        width(1920), height(1080), stride(width), framerate(30)
    { }
};

const char * Encoder::component_name = "video_encode";

struct Region {
    unsigned int left, top, width, height;
};

class Camera : public Component {
protected: 
    static const char * component_name;
    OMX_S32 sharpness;
    OMX_S32 contrast;
    OMX_S32 saturation;
    OMX_U32 brightness;
    OMX_METERINGTYPE metering;
    OMX_S32 exposure_compensation;
    float shutter_speed;
    OMX_BOOL shutter_speed_auto;
    OMX_S32 sensitivity;
    OMX_BOOL sensitivity_auto;
    OMX_EXPOSURECONTROLTYPE exposure_control;
    OMX_BOOL frame_stabilization;
    OMX_WHITEBALCONTROLTYPE white_balance;
    OMX_S32 white_balance_gain_red;
    OMX_S32 white_balance_gain_blue;
    OMX_IMAGEFILTERTYPE image_filter;
    OMX_MIRRORTYPE mirror;
    OMX_S32 rotation;
    OMX_BOOL color_enhancement_enable;
    OMX_U8 color_enhancement_u;
    OMX_U8 color_enhancement_v;
    OMX_BOOL denoise;
    Region roi;
    OMX_DYNAMICRANGEEXPANSIONMODETYPE drc_mode;

    OMX_U32 width;
    OMX_U32 height;
    OMX_U32 framerate;
    OMX_U32 stride;
    OMX_VIDEO_CODINGTYPE compression_format;
    OMX_COLOR_FORMATTYPE color_format;
public:
    Camera() : 
        Component(component_name), sharpness(0), contrast(0), brightness(50),
        metering(OMX_MeteringModeAverage), exposure_compensation(0),
        shutter_speed(1.0/8.0), shutter_speed_auto(OMX_TRUE), 
        sensitivity(100), sensitivity_auto(OMX_TRUE), 
        exposure_control(OMX_ExposureControlAuto), frame_stabilization(OMX_FALSE),
        white_balance(OMX_WhiteBalControlAuto), white_balance_gain_red(1000),
        white_balance_gain_blue(1000), image_filter(OMX_ImageFilterNone),
        mirror(OMX_MirrorNone), rotation(0), color_enhancement_enable(OMX_FALSE),
        color_enhancement_u(128), color_enhancement_v(128), denoise(OMX_FALSE),
        roi{0,0,100,100}, drc_mode(OMX_DynRangeExpOff), width(1920), height(1080),
        framerate(30), stride(width), compression_format(OMX_VIDEO_CodingUnused),
        color_format(OMX_COLOR_FormatYUV420PackedPlanar)
    { }

    void set_sharpness(int sharpness) { this->sharpness = sharpness; }
    int get_sharpness() const { return sharpness; }
    void set_contrast(int contrast) { this->contrast = contrast; }
    int get_contrast() const { return contrast; }
    void set_saturation(int saturation) { this->saturation = saturation; }
    int get_saturation() const { return saturation; }
    void set_brightness(unsigned int brightness) { this->brightness = brightness; }
    unsigned int get_brightness() const { return brightness; }
    void set_metering(OMX_METERINGTYPE metering) { this->metering = metering; }
    OMX_METERINGTYPE get_metering() const { return metering; }
    void set_exposure_compensation(int exposure_compensation) { this->exposure_compensation = exposure_compensation; }
    int get_exposure_compensation() const { return exposure_compensation; }
    void set_shutter_speed(float shutter_speed) { this->shutter_speed = shutter_speed; }
    float get_shutter_speed() const { return shutter_speed; }
    void set_shutter_speed_auto(bool shutter_speed_auto) { this->shutter_speed_auto = shutter_speed_auto ? OMX_TRUE : OMX_FALSE; }
    bool get_shutter_speed_auto() const { return shutter_speed_auto; }
    void set_sensitivity(int sensitivity) { this->sensitivity = sensitivity; }
    int get_sensitivity() const { return sensitivity; }
    void set_sensitivity_auto(bool sensitivity_auto) { this->sensitivity_auto = sensitivity_auto ? OMX_TRUE : OMX_FALSE; }
    bool get_sensitivity_auto() const { return sensitivity_auto; }
    void set_exposure_control(OMX_EXPOSURECONTROLTYPE exposure_control) { this->exposure_control = exposure_control; }
    OMX_EXPOSURECONTROLTYPE get_exposure_control() const { return exposure_control; }
    void set_frame_stabilization(bool frame_stabilization) { this->frame_stabilization = frame_stabilization ? OMX_TRUE : OMX_FALSE; }
    bool get_frame_stabilization() const { return frame_stabilization; }
    void set_white_balance(OMX_WHITEBALCONTROLTYPE white_balance) { this->white_balance = white_balance; }
    OMX_WHITEBALCONTROLTYPE get_white_balance() const { return white_balance; }
    void set_white_balance_gain_red(int white_balance_gain_red) { this->white_balance_gain_red = white_balance_gain_red; }
    int get_white_balance_gain_red() const { return white_balance_gain_red; }
    void set_white_balance_gain_blue(int white_balance_gain_blue) { this->white_balance_gain_blue = white_balance_gain_blue; }
    int get_white_balance_gain_blue() const { return white_balance_gain_blue; }
    void set_image_filter(OMX_IMAGEFILTERTYPE image_filter) { this->image_filter = image_filter; }
    OMX_IMAGEFILTERTYPE get_image_filter() const { return image_filter; }
    void set_mirror(OMX_MIRRORTYPE mirror) { this->mirror = mirror; }
    OMX_MIRRORTYPE get_mirror() const { return mirror; }
    void set_rotation(int rotation) { this->rotation = rotation; }
    int get_rotation() const { return rotation; }
    void set_color_enhancement_enable(bool color_enhancement_enable) { this->color_enhancement_enable = color_enhancement_enable ? OMX_TRUE : OMX_FALSE; }
    bool get_color_enhancement_enable() const { return color_enhancement_enable; }
    void set_color_enhancement_u(unsigned char color_enhancement_u) { this->color_enhancement_u = color_enhancement_u; }
    unsigned char get_color_enhancement_u() const { return color_enhancement_u; }
    void set_color_enhancement_v(unsigned char color_enhancement_v) { this->color_enhancement_v = color_enhancement_v; }
    unsigned char get_color_enhancement_v() const { return color_enhancement_v; }
    void set_denoise(bool denoise) { this->denoise = denoise ? OMX_TRUE : OMX_FALSE; }
    bool get_denoise() const { return denoise; }
    void set_roi(Region roi) { this->roi = roi; }
    Region get_roi() const { return roi; }
    void set_drc_mode(OMX_DYNAMICRANGEEXPANSIONMODETYPE drc_mode) { this->drc_mode = drc_mode; }
    OMX_DYNAMICRANGEEXPANSIONMODETYPE get_drc_mode() const { return drc_mode; }
    
protected:

    static const OMX_U32 port_index = 71;


    void load_camera_drivers() {
        OMX_ERRORTYPE e;

        OMX_CONFIG_REQUESTCALLBACKTYPE cbs;
        OMX_INIT_STRUCTURE(cbs);
        cbs.nPortIndex = OMX_ALL;
        cbs.nIndex = OMX_IndexParamCameraDeviceNumber;
        cbs.bEnable = OMX_TRUE;
        e = OMX_SetConfig(handle, OMX_IndexConfigRequestCallback, &cbs);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SetConfig: %s\n", err2str(e));
            exit(1);
        }

        OMX_PARAM_U32TYPE dev;
        OMX_INIT_STRUCTURE(dev);
        dev.nPortIndex = OMX_ALL;
        dev.nU32 = 0;
        e = OMX_SetParameter(handle, OMX_IndexParamCameraDeviceNumber, &dev);
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_SetParameter: %s\n", err2str(e));
            exit(1);
        }

        wait(EVENT_PARAM_OR_CONFIG_CHANGED);
    }

    void set_camera_settings() {
        OMX_PARAM_PORTDEFINITIONTYPE port_st;
        OMX_INIT_STRUCTURE(port_st);
        port_st.nPortIndex = port_index;
        get_parameter(OMX_IndexParamPortDefinition, &port_st);
        port_st.format.video.nFrameWidth = width;
        port_st.format.video.nFrameHeight = height;
        port_st.format.video.nStride = stride;
        port_st.format.video.xFramerate = framerate << 16;
        port_st.format.video.eCompressionFormat = compression_format;
        port_st.format.video.eColorFormat = color_format;
        set_parameter(OMX_IndexParamPortDefinition, &port_st);

        OMX_CONFIG_FRAMERATETYPE framerate_st;
        OMX_INIT_STRUCTURE(framerate_st);
        framerate_st.nPortIndex = port_index;
        framerate_st.xEncodeFramerate = port_st.format.video.xFramerate;
        set_config(OMX_IndexConfigVideoFramerate, &framerate_st);
        

        OMX_CONFIG_SHARPNESSTYPE sharpness_st;
        OMX_INIT_STRUCTURE(sharpness_st);
        sharpness_st.nPortIndex = OMX_ALL;
        sharpness_st.nSharpness = sharpness;
        set_config(OMX_IndexConfigCommonSharpness, &sharpness_st);

        OMX_CONFIG_CONTRASTTYPE contrast_st;
        OMX_INIT_STRUCTURE(contrast_st);
        contrast_st.nPortIndex = OMX_ALL;
        contrast_st.nContrast = contrast;
        set_config(OMX_IndexConfigCommonContrast, &contrast_st);

        OMX_CONFIG_SATURATIONTYPE saturation_st;
        OMX_INIT_STRUCTURE(saturation_st);
        saturation_st.nPortIndex = OMX_ALL;
        saturation_st.nSaturation = saturation;
        set_config(OMX_IndexConfigCommonSaturation, &saturation_st);

        OMX_CONFIG_BRIGHTNESSTYPE brightness_st;
        OMX_INIT_STRUCTURE(brightness_st);
        brightness_st.nPortIndex = OMX_ALL;
        brightness_st.nBrightness = brightness;
        set_config(OMX_IndexConfigCommonBrightness, &brightness_st);

        OMX_CONFIG_EXPOSUREVALUETYPE exposure_st;
        OMX_INIT_STRUCTURE(exposure_st);
        exposure_st.nPortIndex = OMX_ALL;
        exposure_st.eMetering = metering;
        exposure_st.xEVCompensation = (OMX_S32)((exposure_compensation << 16)/6.0);
        exposure_st.nShutterSpeedMsec = (OMX_U32)((shutter_speed)*1e6);
        exposure_st.bAutoShutterSpeed = shutter_speed_auto;
        exposure_st.nSensitivity = sensitivity;
        exposure_st.bAutoSensitivity = sensitivity_auto;
        set_config(OMX_IndexConfigCommonExposureValue, &exposure_st);

        OMX_CONFIG_EXPOSURECONTROLTYPE exposure_control_st;
        OMX_INIT_STRUCTURE(exposure_control_st);
        exposure_control_st.nPortIndex = OMX_ALL;
        exposure_control_st.eExposureControl = exposure_control;
        set_config(OMX_IndexConfigCommonExposure, &exposure_control_st);

        OMX_CONFIG_FRAMESTABTYPE frame_stabilization_st;
        OMX_INIT_STRUCTURE(frame_stabilization_st);
        frame_stabilization_st.nPortIndex = OMX_ALL;
        frame_stabilization_st.bStab = frame_stabilization;
        set_config(OMX_IndexConfigCommonFrameStabilisation, &frame_stabilization_st);

        OMX_CONFIG_WHITEBALCONTROLTYPE white_balance_st;
        OMX_INIT_STRUCTURE(white_balance_st);
        white_balance_st.nPortIndex = OMX_ALL;
        white_balance_st.eWhiteBalControl = white_balance;
        set_config(OMX_IndexConfigCommonWhiteBalance, &white_balance_st);

        if (!white_balance) {
            OMX_CONFIG_CUSTOMAWBGAINSTYPE white_balance_gains_st;
            OMX_INIT_STRUCTURE(white_balance_gains_st);
            white_balance_gains_st.xGainR = (white_balance_gain_red << 16) / 1000;
            white_balance_gains_st.xGainB = (white_balance_gain_blue << 16) / 1000;
            set_config(OMX_IndexConfigCustomAwbGains, &white_balance_gains_st);
        }

        OMX_CONFIG_IMAGEFILTERTYPE image_filter_st;
        OMX_INIT_STRUCTURE(image_filter_st);
        image_filter_st.nPortIndex = OMX_ALL;
        image_filter_st.eImageFilter = image_filter;
        set_config(OMX_IndexConfigCommonImageFilter, &image_filter_st);

        OMX_CONFIG_MIRRORTYPE mirror_type_st;
        OMX_INIT_STRUCTURE(mirror_type_st);
        mirror_type_st.nPortIndex = port_index;
        mirror_type_st.eMirror = mirror;
        set_config(OMX_IndexConfigCommonMirror, &mirror_type_st);

        OMX_CONFIG_ROTATIONTYPE rotation_st;
        OMX_INIT_STRUCTURE(rotation_st);
        rotation_st.nPortIndex = port_index;
        rotation_st.nRotation = rotation;
        set_config(OMX_IndexConfigCommonRotate, &rotation_st);

        OMX_CONFIG_COLORENHANCEMENTTYPE color_enhancement_st;
        OMX_INIT_STRUCTURE(color_enhancement_st);
        color_enhancement_st.nPortIndex = OMX_ALL;
        color_enhancement_st.bColorEnhancement = color_enhancement_enable;
        color_enhancement_st.nCustomizedU = color_enhancement_u;
        color_enhancement_st.nCustomizedV = color_enhancement_v;
        set_config(OMX_IndexConfigCommonColorEnhancement, &color_enhancement_st);

        OMX_CONFIG_BOOLEANTYPE denoise_st;
        OMX_INIT_STRUCTURE(denoise_st);
        denoise_st.bEnabled = denoise;
        set_config(OMX_IndexConfigStillColourDenoiseEnable, &denoise_st);

        OMX_CONFIG_INPUTCROPTYPE roi_st;
        OMX_INIT_STRUCTURE(roi_st);
        roi_st.nPortIndex = OMX_ALL;
        roi_st.xLeft = (roi.left << 16) / 100;
        roi_st.xTop = (roi.top << 16) / 100;
        roi_st.xWidth = (roi.width << 16) / 100;
        roi_st.xHeight = (roi.height << 16) / 100;
        set_config(OMX_IndexConfigInputCropPercentages, &roi_st);

        OMX_CONFIG_DYNAMICRANGEEXPANSIONTYPE drc_st;
        OMX_INIT_STRUCTURE(drc_st);
        drc_st.eMode = drc_mode;
        set_config(OMX_IndexConfigDynamicRangeExpansion, &drc_st);
    }
};

const char * Camera::component_name = "camera";


void printState(OMX_HANDLETYPE handle) {
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
    case OMX_StateLoaded:           printf("StateLoaded\n"); break;
    case OMX_StateIdle:             printf("StateIdle\n"); break;
    case OMX_StateExecuting:        printf("StateExecuting\n"); break;
    case OMX_StatePause:            printf("StatePause\n"); break;
    case OMX_StateWaitForResources: printf("StateWait\n"); break;
    case OMX_StateInvalid:          printf("StateInvalid\n"); break;
    default:                        printf("State unknown\n"); break;
    }
}

const char *err2str(int err) {
    switch (err) {
    case OMX_ErrorInsufficientResources: return "OMX_ErrorInsufficientResources";
    case OMX_ErrorUndefined: return "OMX_ErrorUndefined";
    case OMX_ErrorInvalidComponentName: return "OMX_ErrorInvalidComponentName";
    case OMX_ErrorComponentNotFound: return "OMX_ErrorComponentNotFound";
    case OMX_ErrorInvalidComponent: return "OMX_ErrorInvalidComponent";
    case OMX_ErrorBadParameter: return "OMX_ErrorBadParameter";
    case OMX_ErrorNotImplemented: return "OMX_ErrorNotImplemented";
    case OMX_ErrorUnderflow: return "OMX_ErrorUnderflow";
    case OMX_ErrorOverflow: return "OMX_ErrorOverflow";
    case OMX_ErrorHardware: return "OMX_ErrorHardware";
    case OMX_ErrorInvalidState: return "OMX_ErrorInvalidState";
    case OMX_ErrorStreamCorrupt: return "OMX_ErrorStreamCorrupt";
    case OMX_ErrorPortsNotCompatible: return "OMX_ErrorPortsNotCompatible";
    case OMX_ErrorResourcesLost: return "OMX_ErrorResourcesLost";
    case OMX_ErrorNoMore: return "OMX_ErrorNoMore";
    case OMX_ErrorVersionMismatch: return "OMX_ErrorVersionMismatch";
    case OMX_ErrorNotReady: return "OMX_ErrorNotReady";
    case OMX_ErrorTimeout: return "OMX_ErrorTimeout";
    case OMX_ErrorSameState: return "OMX_ErrorSameState";
    case OMX_ErrorResourcesPreempted: return "OMX_ErrorResourcesPreempted";
    case OMX_ErrorPortUnresponsiveDuringAllocation: return "OMX_ErrorPortUnresponsiveDuringAllocation";
    case OMX_ErrorPortUnresponsiveDuringDeallocation: return "OMX_ErrorPortUnresponsiveDuringDeallocation";
    case OMX_ErrorPortUnresponsiveDuringStop: return "OMX_ErrorPortUnresponsiveDuringStop";
    case OMX_ErrorIncorrectStateTransition: return "OMX_ErrorIncorrectStateTransition";
    case OMX_ErrorIncorrectStateOperation: return "OMX_ErrorIncorrectStateOperation";
    case OMX_ErrorUnsupportedSetting: return "OMX_ErrorUnsupportedSetting";
    case OMX_ErrorUnsupportedIndex: return "OMX_ErrorUnsupportedIndex";
    case OMX_ErrorBadPortIndex: return "OMX_ErrorBadPortIndex";
    case OMX_ErrorPortUnpopulated: return "OMX_ErrorPortUnpopulated";
    case OMX_ErrorComponentSuspended: return "OMX_ErrorComponentSuspended";
    case OMX_ErrorDynamicResourcesUnavailable: return "OMX_ErrorDynamicResourcesUnavailable";
    case OMX_ErrorMbErrorsInFrame: return "OMX_ErrorMbErrorsInFrame";
    case OMX_ErrorFormatNotDetected: return "OMX_ErrorFormatNotDetected";
    case OMX_ErrorContentPipeOpenFailed: return "OMX_ErrorContentPipeOpenFailed";
    case OMX_ErrorContentPipeCreationFailed: return "OMX_ErrorContentPipeCreationFailed";
    case OMX_ErrorSeperateTablesUsed: return "OMX_ErrorSeperateTablesUsed";
    case OMX_ErrorTunnelingUnsupported: return "OMX_ErrorTunnelingUnsupported";
    default: return "unknown error";
    }
}

int get_file_size(char *fname) {
    struct stat st;

    if (stat(fname, &st) == -1) {
	    perror("Stat'ing img file");
	    return -1;
	}
    return(st.st_size);
}

class App {
    Camera cam;
    Encoder enc;
public:
    App() {
        bcm_host_init();

        OMX_ERRORTYPE e = OMX_Init();
        if (e != OMX_ErrorNone) {
            fprintf(stderr, "error: OMX_Init: %s\n", err2str(e));
            exit(1);
        }


    }

    ~App() {
        bcm_host_deinit();
    }
};

int main(int ac, char ** av) {
    App app();

    return 0;
}