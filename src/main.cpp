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

// Dunno where this is originally stolen from...
#define OMX_INIT_STRUCTURE(a) \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
    (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
    (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
    (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
    (a).nVersion.s.nStep = OMX_VERSION_STEP





// class to keep track of OpenMax state
class App {
    OMX_CALLBACKTYPE callbacks;
    std::mutex m;
    bool flushed;
    bool camera_ready;
    bool encoder_output_buffer_available;

    OMX_HANDLETYPE camera;
    OMX_HANDLETYPE encoder;
    OMX_HANDLETYPE null_sink;

    static OMX_ERRORTYPE empty_buffer_done(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_BUFFERHEADERTYPE* pBuffer) 
    {
        return reinterpret_cast<App*>(pAppData)->empty_buffer_done_(hComponent, pBuffer);
    }

    OMX_ERRORTYPE empty_buffer_done_(OMX_HANDLETYPE hComponent, OMX_BUFFERHEADERTYPE* pBuffer) {
        // do nothing for now
        return OMX_ErrorNone;
    }

    static OMX_ERRORTYPE fill_buffer_done(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_BUFFERHEADERTYPE* pBuffer) 
    {
        App * app = reinterpret_cast<App*>(pAppData);

        return app->fill_buffer_done_(hComponent, pBuffer);
    }

    OMX_ERRORTYPE fill_buffer_done_(
        OMX_HANDLETYPE hComponent,
        OMX_BUFFERHEADERTYPE* pBuffer) 
    {
        std::unique_lock<std::mutex> lk(m);
        encoder_output_buffer_available = true;

        return OMX_ErrorNone;
    }

    static OMX_ERRORTYPE event_handler(
        OMX_HANDLETYPE hComponent,
        OMX_PTR pAppData,
        OMX_EVENTTYPE eEvent,
        OMX_U32 nData1,
        OMX_U32 nData2,
        OMX_PTR pEventData) 
    {
        App * app = reinterpret_cast<App*>(pAppData);

        return app->handle_event_(hComponent, eEvent, nData1, nData2, pEventData);
    }

    OMX_ERRORTYPE handle_event_(
        OMX_HANDLETYPE hComponent,
        OMX_EVENTTYPE eEvent,
        OMX_U32 nData1,
        OMX_U32 nData2,
        OMX_PTR pEventData) 
    {
        switch(eEvent) {
        case OMX_EventCmdComplete:
            if (nData1 == OMX_CommandFlush) {
                set_flushed();
            }
            break;
        case OMX_EventParamOrConfigChanged:
            if(nData2 == OMX_IndexParamCameraDeviceNumber) {
                set_camera_ready();
            }
            break;
        case OMX_EventError:
            throw std::logic_error("event error");
        default:
            // do nothing
            break;
        }

        return OMX_ErrorNone;
    }

    void set_flushed() {
        std::unique_lock<std::mutex> lk(m);
        flushed = true;
    }

    void set_camera_ready() {
        std::unique_lock<std::mutex> lk(m);
        camera_ready = true;
    }

    void block_until_port_changed(OMX_HANDLETYPE hComponent, OMX_U32 nPortIndex, OMX_BOOL bEnabled) {
        OMX_PARAM_PORTDEFINITIONTYPE portdef;
        OMX_INIT_STRUCTURE(portdef);

        portdef.nPortIndex = nPortIndex;
        while(true) {
            OMX_ERRORTYPE r = OMX_GetParameter(hComponent, OMX_IndexParamPortDefinition, &portdef);
            if (r != OMX_ErrorNone) {
                throw std::logic_error("failed to get port definition");
            }

            // TODO: have some exit condition/timeout so this isn't an infinite loop
            if (portdef.bEnabled != bEnabled) {
                usleep(10000);
            } else {
                break;
            }
        }
    }

    void init_component_handle(
        const char * name, 
        OMX_HANDLETYPE *hComponent,
        OMX_CALLBACKTYPE* callbacks)
    {
        OMX_ERRORTYPE e;
        char fullname[32]; 
        memset(fullname, 0, sizeof(fullname));
        snprintf(fullname, sizeof(fullname), "OMX.broadcom.%s", name);

        e = OMX_GetHandle(hComponent, fullname, this, callbacks);
        if (e != OMX_ErrorNone) {
            throw std::logic_error("failed to get handle for component");
        }

        static OMX_INDEXTYPE types[] = {
            OMX_IndexParamAudioInit, OMX_IndexParamVideoInit, OMX_IndexParamImageInit, OMX_IndexParamOtherInit
        };
        OMX_PORT_PARAM_TYPE ports;
        OMX_INIT_STRUCTURE(ports);
        OMX_GetParameter(*hComponent, OMX_IndexParamVideoInit, &ports);

        for(auto type : types) {
            if(OMX_GetParameter(*hComponent, type, &ports) == OMX_ErrorNone) {
                OMX_U32 nPortIndex;
                for(nPortIndex = ports.nStartPortNumber; nPortIndex < ports.nStartPortNumber+ports.nPorts; nPortIndex++) {
                    e = OMX_SendCommand(*hComponent, OMX_CommandPortDisable, nPortIndex, NULL);
                    if (e != OMX_ErrorNone) {
                        throw std::logic_error("failed to disable port");
                    }
                }
                block_until_port_changed(*hComponent, nPortIndex, OMX_FALSE);
            }
        }

    }
public:
    App() {
        bcm_host_init();
        auto err = OMX_Init();
        if (err != OMX_ErrorNone) {
            std::ostrstream msg;
            msg << "could not initialize omx: " << err;
            throw std::logic_error(msg.str());
        }

        memset(&callbacks, 0, sizeof(callbacks));
        callbacks.EventHandler = App::event_handler;
        callbacks.FillBufferDone = App::fill_buffer_done;
        callbacks.EmptyBufferDone = App::empty_buffer_done;

        init_component_handle("camera", &camera, &callbacks);
        init_component_handle("video_encode", &encoder, &callbacks);
        init_component_handle("null_sink", &null_sink, &callbacks);

    }
    ~App() {
        bcm_host_deinit();
    }

public:
};

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

int main(int ac, char ** av) {
    bcm_host_init();

    App app();

    return 0;
}