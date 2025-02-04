
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <cstring>
#include <unistd.h>

#include "resampler.cc"
#include "resampler-table.cc"
#include "zita-resampler/resampler.h"
#include "gx_resampler.cc"

#include "dsp.h"
#include "activations.h"

#include "RTNeural.h"


#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include "lv2/atom/forge.h"
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include "lv2/patch/patch.h"
#include "lv2/options/options.h"
#include "lv2/state/state.h"
#include "lv2/worker/worker.h"
#include <lv2/buf-size/buf-size.h>
///////////////////////// MACRO SUPPORT ////////////////////////////////

#define PLUGIN_URI "urn:brummer:ratatouille"
#define XLV2__MODELFILE "urn:brummer:ratatouille#NAM_Model"
#define XLV2__MODELFILE1 "urn:brummer:ratatouille#NAM_Model1"
#define XLV2__RTMODELFILE "urn:brummer:ratatouille#RTN_Model"
#define XLV2__RTMODELFILE1 "urn:brummer:ratatouille#RTN_Model1"

#define XLV2__GUI "urn:brummer:ratatouille#gui"

using std::min;
using std::max;

typedef int PortIndex;

#include "dcblocker.cc"

////////////////////////////// PLUG-IN CLASS ///////////////////////////

namespace ratatouille {

#include "NeuralAmpMulti.cc"
#include "RtNeuralMulti.cc"


class Xratatouille
{
private:
    dcblocker::Dsp* dcb;
    NeuralAmpMulti rtm;
    RtNeuralMulti  rtnm;
    float* input0;
    float* output0;
    float* _blend;
    double fRec2[2];
    std::string model_file;
    std::string model_file1;
    std::string rtmodel_file;
    std::string rtmodel_file1;
    bool                         doit;
    std::atomic<bool>            _execute;
    std::atomic<bool>            _notify_ui;
    std::atomic<bool>            _restore;
    std::atomic<int>             _ab;
    std::atomic<bool>            _namA;
    std::atomic<bool>            _namB;
    std::atomic<bool>            _rtnA;
    std::atomic<bool>            _rtnB;
    std::condition_variable      CondVar;
    // LV2 stuff
    LV2_URID_Map*                map;
    LV2_Worker_Schedule*         schedule;
    const LV2_Atom_Sequence* control;
    LV2_Atom_Sequence* notify;
    LV2_Atom_Forge forge;
    LV2_Atom_Forge_Frame notify_frame;

    LV2_URID xlv2_model_file;
    LV2_URID xlv2_model_file1;
    LV2_URID xlv2_rtmodel_file;
    LV2_URID xlv2_rtmodel_file1;
    LV2_URID xlv2_gui;
    LV2_URID atom_Object;
    LV2_URID atom_Int;
    LV2_URID atom_Float;
    LV2_URID atom_Bool;
    LV2_URID atom_Vector;
    LV2_URID atom_Path;
    LV2_URID atom_String;
    LV2_URID atom_URID;
    LV2_URID atom_eventTransfer;
    LV2_URID patch_Put;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_property;
    LV2_URID patch_value;
    // private functions
    inline void run_dsp_(uint32_t n_samples);
    inline void connect_(uint32_t port,void* data);
    inline void init_dsp_(uint32_t rate);
    inline void connect_all__ports(uint32_t port, void* data);
    inline void activate_f();
    inline void clean_up();
    inline void do_work_mono();
    inline void deactivate_f();
public:
    inline void map_uris(LV2_URID_Map* map);
    inline LV2_Atom* write_set_file(LV2_Atom_Forge* forge, const LV2_URID xlv2_model, const char* filename);
    inline const LV2_Atom* read_set_file(const LV2_Atom_Object* obj);
    // LV2 Descriptor
    static const LV2_Descriptor descriptor;
    static const void* extension_data(const char* uri);
    // static wrapper to private functions
    static void deactivate(LV2_Handle instance);
    static void cleanup(LV2_Handle instance);
    static void run(LV2_Handle instance, uint32_t n_samples);
    static void activate(LV2_Handle instance);
    static void connect_port(LV2_Handle instance, uint32_t port, void* data);

    static LV2_State_Status save_state(LV2_Handle instance,
                                       LV2_State_Store_Function store,
                                       LV2_State_Handle handle, uint32_t flags,
                                       const LV2_Feature* const* features);

    static LV2_State_Status restore_state(LV2_Handle instance,
                                          LV2_State_Retrieve_Function retrieve,
                                          LV2_State_Handle handle, uint32_t flags,
                                          const LV2_Feature* const*   features);

    static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                                double rate, const char* bundle_path,
                                const LV2_Feature* const* features);
  
    static LV2_Worker_Status work(LV2_Handle                 instance,
                                LV2_Worker_Respond_Function respond,
                                LV2_Worker_Respond_Handle   handle,
                                uint32_t size, const void*    data);
  
    static LV2_Worker_Status work_response(LV2_Handle  instance,
                                         uint32_t    size,
                                         const void* data);
    Xratatouille();
    ~Xratatouille();
};

// constructor
Xratatouille::Xratatouille() :
    dcb(dcblocker::plugin()),
    rtm(&CondVar),
    rtnm(&CondVar),
    input0(NULL),
    output0(NULL) {};

// destructor
Xratatouille::~Xratatouille() {
    dcb->del_instance(dcb);
};

///////////////////////// PRIVATE CLASS  FUNCTIONS /////////////////////

inline void Xratatouille::map_uris(LV2_URID_Map* map) {
    xlv2_model_file = map->map(map->handle, XLV2__MODELFILE);
    xlv2_model_file1 = map->map(map->handle, XLV2__MODELFILE1);
    xlv2_rtmodel_file = map->map(map->handle, XLV2__RTMODELFILE);
    xlv2_rtmodel_file1 = map->map(map->handle, XLV2__RTMODELFILE1);
    xlv2_gui = map->map(map->handle, XLV2__GUI);
    atom_Object = map->map(map->handle, LV2_ATOM__Object);
    atom_Int = map->map(map->handle, LV2_ATOM__Int);
    atom_Float = map->map(map->handle, LV2_ATOM__Float);
    atom_Bool = map->map(map->handle, LV2_ATOM__Bool);
    atom_Vector = map->map(map->handle, LV2_ATOM__Vector);
    atom_Path = map->map(map->handle, LV2_ATOM__Path);
    atom_String = map->map(map->handle, LV2_ATOM__String);
    atom_URID = map->map(map->handle, LV2_ATOM__URID);
    atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
    patch_Put = map->map(map->handle, LV2_PATCH__Put);
    patch_Get = map->map(map->handle, LV2_PATCH__Get);
    patch_Set = map->map(map->handle, LV2_PATCH__Set);
    patch_property = map->map(map->handle, LV2_PATCH__property);
    patch_value = map->map(map->handle, LV2_PATCH__value);
}

void Xratatouille::init_dsp_(uint32_t rate)
{
    dcb->init(rate);
    rtm.init(rate);
    rtnm.init(rate);
    model_file = "None";
    model_file1 = "None";
    rtmodel_file = "None";
    rtmodel_file1 = "None";
    _execute.store(false, std::memory_order_release);
    _notify_ui.store(false, std::memory_order_release);
    _restore.store(false, std::memory_order_release);
    _ab.store(0, std::memory_order_release);
    _namA.store(false, std::memory_order_release);
    _namB.store(false, std::memory_order_release);
    _rtnA.store(false, std::memory_order_release);
    _rtnB.store(false, std::memory_order_release);
    for (int l0 = 0; l0 < 2; l0 = l0 + 1) fRec2[l0] = 0.0;
}

// connect the Ports used by the plug-in class
void Xratatouille::connect_(uint32_t port,void* data)
{
    switch ((PortIndex)port)
    {
        case 0:
            input0 = static_cast<float*>(data);
            break;
        case 1:
            output0 = static_cast<float*>(data);
            break;
        case 4:
            _blend = static_cast<float*>(data);
            break;
        case 5:
            control = (const LV2_Atom_Sequence*)data;
            break;
        case 6:
            notify = (LV2_Atom_Sequence*)data;
            break;
        default:
            break;
    }
}

void Xratatouille::activate_f()
{
    // allocate the internal DSP mem
}

void Xratatouille::clean_up()
{
    for (int l0 = 0; l0 < 2; l0 = l0 + 1) fRec2[l0] = 0.0;
    // delete the internal DSP mem
}

void Xratatouille::deactivate_f()
{
    // delete the internal DSP mem
}

void Xratatouille::do_work_mono()
{
    if (_ab.load(std::memory_order_acquire) == 1) {
        rtm.load_afile = model_file;
        if (!rtm.load_nam_afile()) {
            model_file = "None";
            _namA.store(false, std::memory_order_release);
        } else {
            _namA.store(true, std::memory_order_release);
        }
        rtnm.unload_json_afile();
        rtmodel_file = "None";
        _rtnA.store(false, std::memory_order_release);
    } else if (_ab.load(std::memory_order_acquire) == 2) {
        rtm.load_bfile = model_file1;
        if (!rtm.load_nam_bfile()) {
            model_file1 = "None";
            _namB.store(false, std::memory_order_release);
        } else {
            _namB.store(true, std::memory_order_release);
        }
        rtnm.unload_json_bfile();
        rtmodel_file1 = "None";
        _rtnB.store(false, std::memory_order_release);
    } else if (_ab.load(std::memory_order_acquire) == 3) {
        rtm.load_afile = model_file;
        if (!rtm.load_nam_afile()) {
            model_file = "None";
            _namA.store(false, std::memory_order_release);
        } else {
            _namA.store(true, std::memory_order_release);
        }
        rtm.load_bfile = model_file1;
        if (!rtm.load_nam_bfile()) {
            model_file1 = "None";
            _namB.store(false, std::memory_order_release);
        } else {
            _namB.store(true, std::memory_order_release);
        }
        rtnm.unload_json_afile();
        rtnm.unload_json_bfile();
        rtmodel_file = "None";
        rtmodel_file1 = "None";
        _rtnA.store(false, std::memory_order_release);
        _rtnB.store(false, std::memory_order_release);
    } else if (_ab.load(std::memory_order_acquire) == 4) {
        rtnm.load_afile = rtmodel_file;
        if (!rtnm.load_json_afile()) {
            rtmodel_file = "None";
            _rtnA.store(false, std::memory_order_release);
        } else {
            _rtnA.store(true, std::memory_order_release);
        }
        rtm.unload_nam_afile();
        model_file = "None";
        _namA.store(false, std::memory_order_release);
    } else if (_ab.load(std::memory_order_acquire) == 5) {
        rtnm.load_bfile = rtmodel_file1;
        if (!rtnm.load_json_bfile()) {
            rtmodel_file1 = "None";
            _rtnB.store(false, std::memory_order_release);
        } else {
            _rtnB.store(true, std::memory_order_release);
        }
        rtm.unload_nam_bfile();
        model_file1 = "None";
        _namB.store(false, std::memory_order_release);
    } else if (_ab.load(std::memory_order_acquire) == 6) {
        rtnm.load_afile = rtmodel_file;
        if (!rtnm.load_json_afile()) {
            rtmodel_file = "None";
            _rtnA.store(false, std::memory_order_release);
        } else {
            _rtnA.store(true, std::memory_order_release);
        }
        rtnm.load_bfile = rtmodel_file1;
        if (!rtnm.load_json_bfile()) {
            rtmodel_file1 = "None";
             _rtnB.store(false, std::memory_order_release);
        } else {
            _rtnB.store(true, std::memory_order_release);
        }
        rtm.unload_nam_afile();
        rtm.unload_nam_bfile();
        model_file = "None";
        model_file1 = "None";
        _namA.store(false, std::memory_order_release);
        _namB.store(false, std::memory_order_release);
    } else if (_ab.load(std::memory_order_acquire) > 10) {
        if (model_file != "None") {
            rtm.load_afile = model_file;
            if (!rtm.load_nam_afile()) {
                model_file = "None";
                _namA.store(false, std::memory_order_release);
            } else {
                _namA.store(true, std::memory_order_release);
            }
            rtnm.unload_json_afile();
            rtmodel_file = "None";
            _rtnA.store(false, std::memory_order_release);
        } 
        if (model_file1 != "None") {
            rtm.load_bfile = model_file1;
            if (!rtm.load_nam_bfile()) {
                model_file1 = "None";
                _namB.store(false, std::memory_order_release);
            } else {
                _namB.store(true, std::memory_order_release);
            }
            rtnm.unload_json_bfile();
            rtmodel_file1 = "None";
            _rtnB.store(false, std::memory_order_release);
        } 
        if (rtmodel_file != "None") {
            rtnm.load_afile = rtmodel_file;
            if (!rtnm.load_json_afile()) {
                rtmodel_file = "None";
                _rtnA.store(false, std::memory_order_release);
            } else {
                _rtnA.store(true, std::memory_order_release);
            }
            rtm.unload_nam_afile();
            model_file = "None";
            _namA.store(false, std::memory_order_release);
        }
        if (rtmodel_file1 != "None") {
            rtnm.load_bfile = rtmodel_file1;
            if (!rtnm.load_json_bfile()) {
                rtmodel_file1 = "None";
                _rtnB.store(false, std::memory_order_release);
            } else {
                _rtnB.store(true, std::memory_order_release);
            }
            rtm.unload_nam_bfile();
            model_file1 = "None";
            _namB.store(false, std::memory_order_release);
        }
    }
    _execute.store(false, std::memory_order_release);
    _notify_ui.store(true, std::memory_order_release);
}

inline LV2_Atom* Xratatouille::write_set_file(LV2_Atom_Forge* forge, const LV2_URID xlv2_model, const char* filename) {
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(forge, 0);
    LV2_Atom* set = (LV2_Atom*)lv2_atom_forge_object(
                        forge, &frame, 1, patch_Set);

    lv2_atom_forge_key(forge, patch_property);
    lv2_atom_forge_urid(forge, xlv2_model);
    lv2_atom_forge_key(forge, patch_value);
    lv2_atom_forge_path(forge, filename, strlen(filename) + 1);

    lv2_atom_forge_pop(forge, &frame);
    return set;
}

inline const LV2_Atom* Xratatouille::read_set_file(const LV2_Atom_Object* obj) {
    if (obj->body.otype != patch_Set) {
        return NULL;
    }
    const LV2_Atom* property = NULL;
    lv2_atom_object_get(obj, patch_property, &property, 0);
    if (property && (property->type == atom_URID)) {
        if (((LV2_Atom_URID*)property)->body == xlv2_model_file)
            _ab.store(1, std::memory_order_release);
        else if (((LV2_Atom_URID*)property)->body == xlv2_model_file1)
            _ab.store(2, std::memory_order_release);
        else if (((LV2_Atom_URID*)property)->body == xlv2_rtmodel_file)
            _ab.store(4, std::memory_order_release);
        else if (((LV2_Atom_URID*)property)->body == xlv2_rtmodel_file1)
            _ab.store(5, std::memory_order_release);
        else return NULL;
    }
    const LV2_Atom* file_path = NULL;
    lv2_atom_object_get(obj, patch_value, &file_path, 0);
    if (!file_path || (file_path->type != atom_Path)) {
        return NULL;
    }
    return file_path;
}

void Xratatouille::run_dsp_(uint32_t n_samples)
{
    if(n_samples<1) return;
    const uint32_t notify_capacity = this->notify->atom.size;
    lv2_atom_forge_set_buffer(&forge, (uint8_t*)notify, notify_capacity);
    lv2_atom_forge_sequence_head(&forge, &notify_frame, 0);

    LV2_ATOM_SEQUENCE_FOREACH(control, ev) {
        if (lv2_atom_forge_is_object_type(&forge, ev->body.type)) {
            const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == patch_Get) {
                if (model_file != "None")
                write_set_file(&forge, xlv2_model_file, model_file.data());
                if (model_file1 != "None")
                write_set_file(&forge, xlv2_model_file1, model_file1.data());
                if (rtmodel_file != "None")
                write_set_file(&forge, xlv2_rtmodel_file, rtmodel_file.data());
                if (rtmodel_file1 != "None")
                write_set_file(&forge, xlv2_rtmodel_file1, rtmodel_file1.data());
            } else if (obj->body.otype == patch_Set) {
                const LV2_Atom* file_path = read_set_file(obj);
                if (file_path) {
                    if (_ab.load(std::memory_order_acquire) == 1)
                        model_file = (const char*)(file_path+1);
                    else if (_ab.load(std::memory_order_acquire) == 2)
                        model_file1 = (const char*)(file_path+1);
                    else if (_ab.load(std::memory_order_acquire) == 4)
                        rtmodel_file = (const char*)(file_path+1);
                    else if (_ab.load(std::memory_order_acquire) == 5)
                        rtmodel_file1 = (const char*)(file_path+1);
                    if (!_execute.load(std::memory_order_acquire)) {
                        _execute.store(true, std::memory_order_release);
                        schedule->schedule_work(schedule->handle,  sizeof(bool), &doit);
                    }
                }
            }
        }
    }
    if (!_execute.load(std::memory_order_acquire) && _restore.load(std::memory_order_acquire)) {
        _execute.store(true, std::memory_order_release);
        schedule->schedule_work(schedule->handle,  sizeof(bool), &doit);
        _restore.store(false, std::memory_order_release);
    }

    // do inplace processing on default
    if(output0 != input0)
        memcpy(output0, input0, n_samples*sizeof(float));
    dcb->compute(n_samples, output0, output0);
    float bufa[n_samples];
    memcpy(bufa, output0, n_samples*sizeof(float));
    float bufb[n_samples];
    memcpy(bufb, output0, n_samples*sizeof(float));

    double fSlow2 = 0.0010000000000000009 * double(*(_blend));

    if (_namA.load(std::memory_order_acquire)) {
        rtm.compute(n_samples, bufa, bufa);
        rtnm.compute(n_samples, bufb, bufb);
    } else {
        rtm.compute(n_samples, bufb, bufb);
        rtnm.compute(n_samples, bufa, bufa);        
    }

    if ((_namA.load(std::memory_order_acquire) && _rtnB.load(std::memory_order_acquire)) ||
        (_namB.load(std::memory_order_acquire) && _rtnA.load(std::memory_order_acquire))) {
        for (int i0 = 0; i0 < n_samples; i0 = i0 + 1) {
            fRec2[0] = fSlow2 + 0.999 * fRec2[1];
            output0[i0] = bufa[i0] * (1.0 - fRec2[0]) + bufb[i0] * fRec2[0];
            fRec2[1] = fRec2[0];
        }
    } else if (_namA.load(std::memory_order_acquire) || _rtnA.load(std::memory_order_acquire)) {
        memcpy(output0, bufa, n_samples*sizeof(float));
    } else if (_namB.load(std::memory_order_acquire)) {
        memcpy(output0, bufb, n_samples*sizeof(float));
    } else if (_rtnB.load(std::memory_order_acquire)) {
        memcpy(output0, bufa, n_samples*sizeof(float));
    }

    if (_notify_ui.load(std::memory_order_acquire)) {
        _notify_ui.store(false, std::memory_order_release);
        if (_ab.load(std::memory_order_acquire) == 1) {
            write_set_file(&forge, xlv2_model_file, model_file.data());
        } else if (_ab.load(std::memory_order_acquire) == 2) {
            write_set_file(&forge, xlv2_model_file1, model_file1.data());
        } else if (_ab.load(std::memory_order_acquire) == 3) {
            write_set_file(&forge, xlv2_model_file, model_file.data());
            write_set_file(&forge, xlv2_model_file1, model_file1.data());
        } else if (_ab.load(std::memory_order_acquire) == 4) {
            write_set_file(&forge, xlv2_rtmodel_file, rtmodel_file.data());
        } else if (_ab.load(std::memory_order_acquire) == 5) {
            write_set_file(&forge, xlv2_rtmodel_file1, rtmodel_file1.data());
        } else if (_ab.load(std::memory_order_acquire) == 6) {
            write_set_file(&forge, xlv2_rtmodel_file, rtmodel_file.data());
            write_set_file(&forge, xlv2_rtmodel_file1, rtmodel_file1.data());
        }
        _ab.store(0, std::memory_order_release);
    }
    CondVar.notify_all();
}

void Xratatouille::connect_all__ports(uint32_t port, void* data)
{
    // connect the Ports used by the plug-in class
    connect_(port,data);
    rtm.connect(port,data);
    rtnm.connect(port,data);
}

////////////////////// STATIC CLASS  FUNCTIONS  ////////////////////////

LV2_State_Status Xratatouille::save_state(LV2_Handle instance,
                                     LV2_State_Store_Function store,
                                     LV2_State_Handle handle, uint32_t flags,
                                     const LV2_Feature* const* features) {

    Xratatouille* self = static_cast<Xratatouille*>(instance);

    store(handle,self->xlv2_model_file,self->model_file.data(), strlen(self->model_file.data()) + 1,
          self->atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    store(handle,self->xlv2_model_file1,self->model_file1.data(), strlen(self->model_file1.data()) + 1,
          self->atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    store(handle,self->xlv2_rtmodel_file,self->rtmodel_file.data(), strlen(self->rtmodel_file.data()) + 1,
          self->atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    store(handle,self->xlv2_rtmodel_file1,self->rtmodel_file1.data(), strlen(self->rtmodel_file1.data()) + 1,
          self->atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    return LV2_STATE_SUCCESS;
}



LV2_State_Status Xratatouille::restore_state(LV2_Handle instance,
                                        LV2_State_Retrieve_Function retrieve,
                                        LV2_State_Handle handle, uint32_t flags,
                                        const LV2_Feature* const*   features) {

    Xratatouille* self = static_cast<Xratatouille*>(instance);

    size_t      size;
    uint32_t    type;
    uint32_t    fflags;
    const void* name = retrieve(handle, self->xlv2_model_file, &size, &type, &fflags);

    if (name) {
        self->model_file = (const char*)(name);
        if (!self->model_file.empty() && (self->model_file != "None")) {
            self->_ab.fetch_add(1, std::memory_order_relaxed);
        }
    }

    name = retrieve(handle, self->xlv2_model_file1, &size, &type, &fflags);

    if (name) {
        self->model_file1 = (const char*)(name);
        if (!self->model_file1.empty() && (self->model_file1 != "None")) {
            self->_ab.fetch_add(2, std::memory_order_relaxed);
        }
    }

    name = retrieve(handle, self->xlv2_rtmodel_file, &size, &type, &fflags);

    if (name) {
        self->rtmodel_file = (const char*)(name);
        if (!self->rtmodel_file.empty() && (self->rtmodel_file != "None")) {
            self->_ab.fetch_add(12, std::memory_order_relaxed);
        }
    }

    name = retrieve(handle, self->xlv2_rtmodel_file1, &size, &type, &fflags);

    if (name) {
        self->rtmodel_file1 = (const char*)(name);
        if (!self->rtmodel_file1.empty() && (self->rtmodel_file1 != "None")) {
            self->_ab.fetch_add(12, std::memory_order_relaxed);
        }
    }
    self-> _restore.store(true, std::memory_order_release);
    return LV2_STATE_SUCCESS;
}

LV2_Handle 
Xratatouille::instantiate(const LV2_Descriptor* descriptor,
                            double rate, const char* bundle_path,
                            const LV2_Feature* const* features)
{
    // init the plug-in class
    Xratatouille *self = new Xratatouille();
    if (!self) {
        return NULL;
    }

    const LV2_Options_Option* options  = NULL;
    uint32_t bufsize = 0;

    for (int32_t i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        }
        else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
            self->schedule = (LV2_Worker_Schedule*)features[i]->data;
        }
        else if (!strcmp(features[i]->URI, LV2_OPTIONS__options)) {
            options = (const LV2_Options_Option*)features[i]->data;
        }
    }
    if (!self->schedule) {
        fprintf(stderr, "Missing feature work:schedule.\n");
        self->_execute.store(true, std::memory_order_release);
    }
    if (!self->map) {
        fprintf(stderr, "Missing feature uri:map.\n");
    }
    else if (!options) {
        fprintf(stderr, "Missing feature options.\n");
    }
    else {
        LV2_URID bufsz_max = self->map->map(self->map->handle, LV2_BUF_SIZE__maxBlockLength);
        LV2_URID bufsz_    = self->map->map(self->map->handle,"http://lv2plug.in/ns/ext/buf-size#nominalBlockLength");
        LV2_URID atom_Int = self->map->map(self->map->handle, LV2_ATOM__Int);

        for (const LV2_Options_Option* o = options; o->key; ++o) {
            if (o->context == LV2_OPTIONS_INSTANCE &&
              o->key == bufsz_ && o->type == atom_Int) {
                bufsize = *(const int32_t*)o->value;
            } else if (o->context == LV2_OPTIONS_INSTANCE &&
              o->key == bufsz_max && o->type == atom_Int) {
                if (!bufsize)
                    bufsize = *(const int32_t*)o->value;
            }
        }

        if (bufsize == 0) {
            fprintf(stderr, "No maximum buffer size given.\n");
        } else {
            printf("using block size: %d\n", bufsize);
        }
    }
    self->map_uris(self->map);
    lv2_atom_forge_init(&self->forge, self->map);
    self->init_dsp_((uint32_t)rate);
    return (LV2_Handle)self;
}

void Xratatouille::connect_port(LV2_Handle instance, 
                                   uint32_t port, void* data)
{
    // connect all ports
    static_cast<Xratatouille*>(instance)->connect_all__ports(port, data);
}

void Xratatouille::activate(LV2_Handle instance)
{
    // allocate needed mem
    static_cast<Xratatouille*>(instance)->activate_f();
}

void Xratatouille::run(LV2_Handle instance, uint32_t n_samples)
{
    // run dsp
    static_cast<Xratatouille*>(instance)->run_dsp_(n_samples);
}

void Xratatouille::deactivate(LV2_Handle instance)
{
    // free allocated mem
    static_cast<Xratatouille*>(instance)->deactivate_f();
}

void Xratatouille::cleanup(LV2_Handle instance)
{
    // well, clean up after us
    Xratatouille* self = static_cast<Xratatouille*>(instance);
    self->clean_up();
    delete self;
}

LV2_Worker_Status Xratatouille::work(LV2_Handle instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle   handle,
     uint32_t                    size,
     const void*                 data)
{
  static_cast<Xratatouille*>(instance)->do_work_mono();
  return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status Xratatouille::work_response(LV2_Handle instance,
              uint32_t    size,
              const void* data)
{
  //printf("worker respose.\n");
  return LV2_WORKER_SUCCESS;
}

const void* Xratatouille::extension_data(const char* uri)
{
    static const LV2_Worker_Interface worker = { work, work_response, NULL };
    static const LV2_State_Interface  state  = { save_state, restore_state };
    if (!strcmp(uri, LV2_WORKER__interface)) {
        return &worker;
    }
    else if (!strcmp(uri, LV2_STATE__interface)) {
        return &state;
    }
    return NULL;
}

const LV2_Descriptor Xratatouille::descriptor =
{
    PLUGIN_URI ,
    Xratatouille::instantiate,
    Xratatouille::connect_port,
    Xratatouille::activate,
    Xratatouille::run,
    Xratatouille::deactivate,
    Xratatouille::cleanup,
    Xratatouille::extension_data
};

} // end namespace ratatouille

////////////////////////// LV2 SYMBOL EXPORT ///////////////////////////

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index)
    {
        case 0:
            return &ratatouille::Xratatouille::descriptor;
        default:
            return NULL;
    }
}

///////////////////////////// FIN //////////////////////////////////////
