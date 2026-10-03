#include "DxEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> bank() {
    std::vector<std::uint8_t> data {'D','X','P','1',1,0,0,0,83,103,5,1,127,64,64,0};
    std::vector<std::uint8_t> patch(155);
    for (unsigned op = 0; op < 6; ++op) {
        for (unsigned i = 0; i < 4; ++i) patch[op * 21 + i] = 99;
        for (unsigned i = 4; i < 7; ++i) patch[op * 21 + i] = 99;
        patch[op * 21 + 16] = op == 0 ? 99 : 0;
        patch[op * 21 + 18] = 1; patch[op * 21 + 20] = 7;
    }
    for (unsigned i = 126; i < 130; ++i) patch[i] = 99;
    for (unsigned i = 130; i < 134; ++i) patch[i] = 50;
    patch[134] = 31; patch[144] = 24;
    data.insert(data.end(), patch.begin(), patch.end()); return data;
}
void setup(hybrid::DxEngine& engine) {
    engine.queueShort(0x53'00'b0, 0); engine.queueShort(0x67'20'b0, 0);
    engine.queueShort(0x05'c0, 0);
}
void documentedPartTests() {
    std::vector<float> buses(16*1024);
    const auto part = [](hybrid::DxEngine& e, unsigned address, unsigned value) {
        e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,8,0,
            static_cast<std::uint8_t>(address),static_cast<std::uint8_t>(value),0xf7},0);
    };
    for (unsigned address : {0x0fu,0x10u,0x6du,0x6eu}) {
        hybrid::DxEngine e(bank()); setup(e);
        part(e,address,address == 0x0f || address == 0x6d ? 110 : 30);
        e.queueShort(0x64'45'90,0); e.render(buses.data(),1024,1024);
        require(e.activeVoices()==0,"DX note/velocity limit ignored");
    }
    hybrid::DxEngine e(bank()); setup(e);
    e.queueShort(0x64'45'90,0); e.queueShort(0x7f'42'b0,100);
    e.queueShort(0x64'48'90,200);
    e.queueShort(0x00'45'80,300); e.queueShort(0x00'48'80,300);
    for (unsigned i=0;i<100;++i) e.render(buses.data(),1024,1024);
    require(e.activeVoices()==1,"DX sostenuto must hold only notes sounding when pressed");
    e.queueShort(0x7f'40'b0,102400); e.queueShort(0x00'42'b0,102400);
    for (unsigned i=0;i<100;++i) e.render(buses.data(),1024,1024);
    require(e.activeVoices()==1,"Sustain must retain a note released by sostenuto");
    e.queueShort(0x00'79'b0,204800);
    for (unsigned i=0;i<100;++i) e.render(buses.data(),1024,1024);
    require(e.activeVoices()==0,"Reset All Controllers left sostenuto/sustain notes stuck");
    for (unsigned cc : {124u,125u}) {
        hybrid::DxEngine omni(bank()); setup(omni);
        omni.queueShort(0x64'45'90,0); omni.queueShort(0xb0|(cc<<8),100);
        for (unsigned i=0;i<100;++i) omni.render(buses.data(),1024,1024);
        require(omni.activeVoices()==0,"DX Omni mode message must release notes");
    }
}
std::vector<float> render(unsigned block, unsigned rate, bool wet = false, int bendRange = -1) {
    hybrid::DxEngine engine(bank()); engine.setSampleRate(static_cast<float>(rate)); setup(engine);
    if (bendRange >= 0) {
        engine.queueShort(0x00'65'b0, 0); engine.queueShort(0x00'64'b0, 0);
        engine.queueShort(0xb0 | (6 << 8) | (static_cast<unsigned>(bendRange) << 16), 0);
        engine.queueShort(0x7f'7f'e0, 0);
    }
    engine.queueShort(0x64'45'90, 101);
    engine.queueShort(wet ? 0x7f'5b'b0 : 0x00'5b'b0, 0);
    engine.queueShort(0x00'45'80, rate);
    std::vector<float> result;
    std::vector<float> buses(block * 16);
    for (unsigned frame = 0; frame < rate * 2;) {
        const auto count = std::min(block, rate * 2 - frame);
        engine.render(buses.data(), block, count);
        result.insert(result.end(), buses.begin(), buses.begin() + count); frame += count;
        if (wet && frame > 1000 && frame < rate) require(
            std::any_of(buses.begin() + block * 2, buses.begin() + block * 3,
                [](float x) { return std::abs(x) > 0.0001f; }), "Reverb bus silent");
    }
    require(engine.activeVoices() == 0, "Released FM voices were not retired");
    return result;
}
void routingTests() {
    for (unsigned target = 0; target < 4; ++target) {
        hybrid::DxEngine direct(bank()); setup(direct);
        direct.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,3,
            static_cast<std::uint8_t>(target),0,0x47,0,0xf7}, 0);
        direct.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,3,
            static_cast<std::uint8_t>(target),0x0c,0,0xf7}, 0);
        direct.queueShort(0x64'45'90, 0);
        std::vector<float> buses(16 * 1024);
        direct.render(buses.data(), 1024, 1024);
        require(std::all_of(buses.begin(), buses.begin() + 1024,
            [](float x) { return x == 0; }), "Direct MU insertion double-routed to dry");
        const unsigned bus = 8 + target * 2;
        require(std::any_of(buses.begin() + bus * 1024, buses.begin() + (bus + 1) * 1024,
            [](float x) { return std::abs(x) > 0.0001f; }), "Direct MU insertion silent");
    }
    for (unsigned target = 1; target <= 4; ++target) {
        hybrid::DxEngine engine(bank()); setup(engine);
        std::vector<std::uint8_t> type {0xf0,0x43,0x10,0x4c,
            static_cast<std::uint8_t>(target == 1 ? 2 : 3),
            static_cast<std::uint8_t>(target == 1 ? 1 : target - 1),
            static_cast<std::uint8_t>(target == 1 ? 0x40 : 0),0x49,0,0xf7};
        auto assign = type;
        assign[6] = target == 1 ? 0x5b : 0x0c;
        assign[7] = 0; assign.erase(assign.begin() + 8);
        engine.queueSysex(type, 0); engine.queueSysex(assign, 0);
        engine.queueShort(0x64'45'90, 0);
        std::vector<float> buses(16 * 1024);
        engine.render(buses.data(), 1024, 1024);
        const unsigned bus = 8 + (target - 1) * 2;
        require(std::all_of(buses.begin(), buses.begin() + 1024, [](float x) { return x == 0; }), "Insertion double-routed to dry");
        require(std::any_of(buses.begin() + bus * 1024, buses.begin() + (bus + 1) * 1024,
            [](float x) { return std::abs(x) > 0.0001f; }), "DX insertion bus silent");
    }
    hybrid::DxEngine engine(bank()); setup(engine);
    engine.queueShort(0x64'45'90, 0); engine.queueShort(0x64'45'90, 100);
    engine.queueShort(0x00'45'80, 200);
    std::vector<float> buses(16 * 1024); engine.render(buses.data(), 1024, 1024);
    require(engine.activeVoices() >= 1, "One note-off stopped every repeated note");
    engine.queueShort(0x00'45'80, 1024); engine.render(buses.data(), 1024, 1024);
    for (int i = 0; i < 100; ++i) engine.render(buses.data(), 1024, 1024);
    require(engine.activeVoices() == 0, "Repeated notes stuck");
    hybrid::DxEngine pedal(bank()); setup(pedal);
    pedal.queueShort(0x7f'40'b0, 0); pedal.queueShort(0x64'45'90, 0);
    pedal.queueShort(0x00'45'80, 200);
    pedal.render(buses.data(), 1024, 1024);
    require(pedal.activeVoices() == 1, "Sustain did not hold note");
    pedal.queueShort(0x00'40'b0, 1024);
    for (int i = 0; i < 100; ++i) pedal.render(buses.data(), 1024, 1024);
    require(pedal.activeVoices() == 0, "Sustain release stuck");
    hybrid::DxEngine custom(bank());
    const auto source = bank();
    std::vector<std::uint8_t> dump {0xf0,0x43,0,0,1,27};
    dump.insert(dump.end(), source.begin() + 16, source.end());
    unsigned checksum = 0;
    for (unsigned i = 6; i < dump.size(); ++i) checksum += dump[i];
    dump.push_back(static_cast<std::uint8_t>((128 - (checksum & 127)) & 127));
    dump.push_back(0xf7);
    custom.queueSysex(dump, 0);
    custom.queueShort(0x53'00'b0, 0); custom.queueShort(0x02'20'b0, 0);
    custom.queueShort(0x00'c0, 0); custom.queueShort(0x64'45'90, 0);
    custom.render(buses.data(), 1024, 1024);
    require(custom.activeVoices() == 1, "Valid VCED edit-buffer dump not loaded");
    hybrid::DxEngine damaged(bank()); dump[161] ^= 1;
    damaged.queueSysex(dump, 0);
    damaged.queueShort(0x53'00'b0, 0); damaged.queueShort(0x02'20'b0, 0);
    damaged.queueShort(0x00'c0, 0); damaged.queueShort(0x64'45'90, 0);
    damaged.render(buses.data(), 1024, 1024);
    require(damaged.activeVoices() == 0, "Bad VCED checksum accepted");
}
void partEffectsTests() {
    const auto audio = [](bool useSysex, bool bulk, bool corrupt = false) {
        hybrid::DxEngine engine(bank()); setup(engine);
        if (useSysex) {
            // MU and PLG manuals agree: 11 dry, 12 chorus, 13 reverb, 14 variation.
            std::vector<std::uint8_t> message = bulk
                ? std::vector<std::uint8_t> {0xf0,0x43,0,0x4c,0,4,8,0,0x11,31,63,95,127}
                : std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,8,0,0x11,31,63,95,127};
            if (bulk) {
                unsigned sum = 0;
                for (unsigned i = 4; i < message.size(); ++i) sum += message[i];
                message.push_back(static_cast<std::uint8_t>((128 - (sum & 127)) & 127));
                if (corrupt) message.back() ^= 1;
            }
            message.push_back(0xf7);
            engine.queueSysex(message, 0);
        } else {
            engine.queueShort(0x3f'5d'b0, 0); engine.queueShort(0x5f'5b'b0, 0);
            engine.queueShort(0x7f'5e'b0, 0);
            engine.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,8,0,0x11,31,0xf7}, 0);
        }
        engine.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,2,1,0x5a,1,0xf7}, 0);
        engine.queueShort(0x64'45'90, 0);
        std::vector<float> buses(16 * 1024);
        engine.render(buses.data(), 1024, 1024);
        return buses;
    };
    const auto cc = audio(false, false);
    require(cc == audio(true, false), "Part effect SysEx disagrees with equivalent CC sends");
    require(cc == audio(true, true), "Checked XG bulk effect sends not applied");
    require(cc != audio(true, true, true), "Corrupt XG bulk checksum accepted");
    const auto peak = [](const auto& a, unsigned bus) {
        float p = 0;
        for (unsigned i = bus * 1024; i < (bus + 1) * 1024; ++i) p = std::max(p, std::abs(a[i]));
        return p;
    };
    require(std::abs(peak(cc, 2) / peak(cc, 6) - 95.0f / 127) < 0.0001f, "Reverb address wrong");
    require(std::abs(peak(cc, 4) / peak(cc, 6) - 63.0f / 127) < 0.0001f, "Chorus address wrong");
    require(std::abs(peak(cc, 0) / peak(cc, 6) - 31.0f / 127) < 0.0001f, "Dry level ignored");
    hybrid::DxEngine system(bank()); setup(system);
    system.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,2,1,0x40,5,0,0xf7},0);
    system.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,2,1,0x5a,1,0xf7},0);
    system.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x4c,2,1,0x5b,0,0xf7},0);
    system.queueShort(0x7f'5e'b0,0); system.queueShort(0x64'45'90,0);
    std::vector<float> buses(16*1024); system.render(buses.data(),1024,1024);
    require(peak(buses,0) > 0 && peak(buses,6) > 0 && peak(buses,8) == 0,
        "System variation incorrectly routed into insertion 1");
}
void nativeControlTests() {
    const auto audio = [](bool edited, bool reset) {
        hybrid::DxEngine engine(bank()); setup(engine);
        engine.queueShort(0x64'45'90, 0);
        if (edited) engine.queueSysex(std::vector<std::uint8_t>
            {0xf0,0x43,0x10,0x62,0x60,0,0x10,0,0xf7}, 2048);
        if (reset) engine.queueSysex(std::vector<std::uint8_t>
            {0xf0,0x43,0x10,0x62,0x60,0,0x10,64,0xf7}, 4096);
        std::vector<float> buses(16 * 8192);
        engine.render(buses.data(), 8192, 8192);
        return buses;
    };
    const auto original = audio(false, false), edited = audio(true, false), restored = audio(true, true);
    require(std::equal(original.begin(), original.begin() + 2048, edited.begin()), "Native edit applied early");
    require(!std::equal(original.begin() + 2500, original.begin() + 4000, edited.begin() + 2500), "Native carrier edit ignored on held note");
    require(std::equal(original.begin() + 4500, original.begin() + 8000, restored.begin() + 4500), "Native carrier centre value not restored");
}
void userBankTests() {
    const auto load = [](bool valid, unsigned block) {
        hybrid::DxEngine engine(bank());
        engine.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,0x62,0,0,0xe,
            static_cast<std::uint8_t>(block),0xf7}, 0);
        // Valid packed sine voices, all six independent carriers (algorithm 32).
        std::vector<std::uint8_t> packed(128);
        for (unsigned op = 0; op < 6; ++op) {
            for (unsigned i = 0; i < 4; ++i) packed[op * 17 + i] = 99;
            for (unsigned i = 4; i < 7; ++i) packed[op * 17 + i] = 99;
            packed[op * 17 + 12] = 7 << 3;
            packed[op * 17 + 14] = op == 0 ? 99 : 0;
            packed[op * 17 + 15] = 2;
        }
        for (unsigned i = 102; i < 106; ++i) packed[i] = 99;
        for (unsigned i = 106; i < 110; ++i) packed[i] = 50;
        packed[110] = 31; packed[117] = 24;
        std::vector<std::uint8_t> dump {0xf0,0x43,0,9,32,0};
        for (unsigned i = 0; i < 32; ++i) dump.insert(dump.end(), packed.begin(), packed.end());
        unsigned sum = 0;
        for (unsigned i = 6; i < dump.size(); ++i) sum += dump[i];
        dump.push_back(static_cast<std::uint8_t>((128 - (sum & 127)) & 127)); dump.push_back(0xf7);
        if (!valid) dump[dump.size()-2] ^= 1;
        engine.queueSysex(dump, 0);
        engine.queueShort(0x23'00'b0, 0); engine.queueShort(0x00'20'b0, 0);
        engine.queueShort(0xc0 | ((block * 32 + 31) << 8), 0); engine.queueShort(0x64'45'90, 0);
        std::vector<float> buses(16 * 1024); engine.render(buses.data(), 1024, 1024);
        return engine.activeVoices();
    };
    require(load(true, 0) == 1 && load(true, 1) == 1, "32-voice VMEM user bank not loaded");
    require(load(false, 0) == 0, "Corrupt VMEM bank accepted");
}
void pluginVoiceTests() {
    const auto bulk = [](unsigned high, unsigned mid, unsigned low, std::vector<std::uint8_t> data) {
        std::vector<std::uint8_t> result {0xf0,0x43,0,0x64,
            static_cast<std::uint8_t>(data.size()/128),static_cast<std::uint8_t>(data.size()%128),
            static_cast<std::uint8_t>(high),static_cast<std::uint8_t>(mid),static_cast<std::uint8_t>(low)};
        result.insert(result.end(),data.begin(),data.end()); unsigned sum = 0;
        for (unsigned i = 4; i < result.size(); ++i) sum += result[i];
        result.push_back(static_cast<std::uint8_t>((128-(sum&127))&127)); result.push_back(0xf7);
        return result;
    };
    for (bool corrupt : {false, true}) {
        hybrid::DxEngine engine(bank());
        engine.queueSysex(bulk(0xe,0x1f,0,{}),0);
        std::vector<std::uint8_t> element(35); element[0]=83; element[1]=103; element[2]=5;
        auto message = bulk(0x4c,0x10,0,element);
        if (corrupt) message[message.size()-2] ^= 1;
        engine.queueSysex(message,0);
        require(!engine.selected(0), "Model-64 bank committed before checked footer");
        engine.queueSysex(bulk(0xf,0x1f,0,{}),0);
        require(engine.selected(0) != corrupt, "Model-64 transaction or checksum ignored");
        engine.queueShort(0x64'45'90,0);
        std::vector<float> buses(16*1024); engine.render(buses.data(),1024,1024);
        require(engine.activeVoices() == (corrupt ? 0 : 1), "Model-64 voice not heard by render path");
    }
}
void integrationSafetyTests() {
    const auto dump = [] {
        const auto source = bank();
        std::vector<std::uint8_t> bytes {0xf0,0x43,0,0,1,27};
        bytes.insert(bytes.end(), source.begin() + 16, source.end());
        unsigned sum = 0;
        for (unsigned i = 6; i < bytes.size(); ++i) sum += bytes[i];
        bytes.push_back((128 - (sum & 127)) & 127); bytes.push_back(0xf7);
        return bytes;
    }();
    hybrid::DxEngine disabled(bank());
    disabled.queueSysex(dump,0);
    disabled.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,1,27,0,0xf7},0);
    disabled.queueShort(0x53'00'b0,0); disabled.queueShort(0x02'20'b0,0);
    disabled.queueShort(0x00'c0,0); disabled.queueShort(0x64'45'90,0);
    std::vector<float> buses(16*8192);
    disabled.render(buses.data(),8192,8192);
    require(std::all_of(buses.begin(),buses.end(),[](float x){return x == 0;}),
        "VCED all-operators-off mask ignored");
    for (unsigned mask : {1u,32u}) {
        hybrid::DxEngine oneOperator(bank()); oneOperator.queueSysex(dump,0);
        oneOperator.queueSysex(std::vector<std::uint8_t> {0xf0,0x43,0x10,1,27,
            static_cast<std::uint8_t>(mask),0xf7},0);
        oneOperator.queueShort(0x53'00'b0,0); oneOperator.queueShort(0x02'20'b0,0);
        oneOperator.queueShort(0x00'c0,0); oneOperator.queueShort(0x64'45'90,0);
        oneOperator.render(buses.data(),8192,8192);
        const bool audible=std::any_of(buses.begin(),buses.end(),[](float x){return std::abs(x)>0.001f;});
        require(audible == (mask==1),"Operator-enable bit order disagrees with Yamaha OP6 bit 0");
    }

    hybrid::DxEngine ownership(bank()); setup(ownership);
    require(!ownership.queueShort(0x00'45'80,0),
        "DX swallowed note-off needed by an earlier MU/VL/SG voice");
    const auto acrossChange=[](bool change) {
        hybrid::DxEngine engine(bank()); setup(engine); engine.queueShort(0x64'45'90,0);
        if (change) { engine.queueShort(0x00'00'b0,512); engine.queueShort(0x00'c0,512); }
        engine.queueShort(0x00'45'80,8192);
        std::vector<float> result, output(16*512);
        for(unsigned i=0;i<40;++i) {
            engine.render(output.data(),512,512);
            result.insert(result.end(),output.begin(),output.begin()+512);
        }
        require(engine.activeVoices()==0,"Retained pre-patch voice did not release");
        return result;
    };
    require(acrossChange(false)==acrossChange(true),"Patch switch prematurely ended an existing DX note");

    hybrid::DxEngine pooled(bank());
    pooled.queueSysex(dump,100000);
    std::vector<std::uint8_t> noise(4096,0);
    for (unsigned i=0;i<600;++i) {
        pooled.queueSysex(noise,i*64); pooled.render(buses.data(),8192,64);
    }
    // Compaction must preserve a far-future voice dump, not overwrite it with noise.
    for (unsigned i=0;i<1000;++i) pooled.render(buses.data(),8192,64);
    pooled.queueShort(0x53'00'b0,102400); pooled.queueShort(0x02'20'b0,102400);
    pooled.queueShort(0x00'c0,102400); pooled.queueShort(0x64'45'90,102400);
    pooled.render(buses.data(),8192,64);
    require(pooled.activeVoices()==1,"SysEx compaction lost the retained future voice dump");
    auto slow = bank(); slow[16+3] = 0;
    hybrid::DxEngine held(slow); setup(held);
    held.queueShort(0x64'45'90,0);
    constexpr unsigned heldFrames=44100*121;
    for (unsigned frame=0;frame<heldFrames;) {
        const auto count=std::min(8192u,heldFrames-frame);
        held.render(buses.data(),8192,count); frame+=count;
    }
    held.queueShort(0x00'45'80,heldFrames); held.render(buses.data(),8192,512);
    require(held.activeVoices()==1,"Long-held note lost its release tail after 120 seconds");
}
void extendedPitchTests() {
    const auto audio = [](unsigned mode) {
        hybrid::DxEngine engine(bank()); setup(engine);
        if (mode == 1) {
            engine.queueShort(0x00'65'b0,0); engine.queueShort(0x02'64'b0,0);
            engine.queueShort(0x4c'06'b0,0);
        }
        if (mode == 2 || mode == 3) {
            engine.queueSysex(std::vector<std::uint8_t>
                {0xf0,0x43,0x10,0x4c,8,0,0x4d,76,0xf7},0);
            engine.queueShort(0x7f'd0,mode == 2 ? 0 : 2048);
        }
        if (mode == 4) {
            engine.queueSysex(std::vector<std::uint8_t>
                {0xf0,0x43,0x10,0x4c,8,0,0x53,76,0xf7},0);
            engine.queueShort(0x7f'45'a0,0);
        }
        if (mode == 5) {
            engine.queueSysex(std::vector<std::uint8_t>
                {0xf0,0x43,0x10,0x4c,8,0,0x23,52,0xf7},0);
            engine.queueShort(0x00'00'e0,0);
        }
        engine.queueShort(0x64'45'90,0);
        std::vector<float> buses(16*8192); engine.render(buses.data(),8192,8192);
        return std::vector<float>(buses.begin(),buses.begin()+8192);
    };
    const auto count=[](const auto& x) { unsigned n=0; for(unsigned i=4097;i<8192;++i) if(x[i-1]<=0 && x[i]>0) ++n; return n; };
    const auto base=audio(0);
    for(unsigned mode : {1u,2u,4u,5u})
        require(std::abs(static_cast<int>(count(audio(mode)))-2*static_cast<int>(count(base)))<=2,
            "RPN coarse tuning, pressure pitch mapping or reversed bend range ignored");
    const auto late=audio(3);
    require(std::equal(base.begin(),base.begin()+2048,late.begin()),"Aftertouch applied early");
}
void sweep(const char* path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> data {std::istreambuf_iterator<char>(file), {}};
    hybrid::DxEngine engine(data);
    std::vector<float> buses(16 * 4096);
    unsigned pos = 8, count = engine.selections(), audible = 0;
    std::uint64_t frame = 0;
    float peak = 0;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned msb = data[pos++], lsb = data[pos++], pc = data[pos++], layers = data[pos++];
        pos += layers * 159;
        engine.queueShort(0x00'78'b0, frame);
        engine.queueShort(0xb0 | (msb << 16), frame);
        engine.queueShort(0x20'b0 | (lsb << 16), frame);
        engine.queueShort(0xc0 | (pc << 8), frame);
        engine.queueShort(0x64'3c'90, frame);
        engine.render(buses.data(), 4096, 4096); frame += 4096;
        bool sound = false;
        for (auto sample : buses) {
            require(std::isfinite(sample), "Recovered voice produced nonfinite audio");
            sound |= std::abs(sample) > 0.00001f;
            peak = std::max(peak, std::abs(sample));
        }
        audible += sound;
    }
    std::cout << "Recovered bank sweep: selections=" << count << " audible-in-93ms=" << audible << " peak=" << peak << '\n';
}
int main(int argc, char** argv) try {
    auto data = bank();
    hybrid::DxEngine engine(data);
    require(engine.selections() == 1, "Selection count incorrect");
    engine.queueShort(0x53'00'b0, 0);
    require(!engine.selected(0), "Bank committed before program change");
    engine.queueShort(0x05'c0, 0); require(engine.selected(0), "DX not selected");
    for (unsigned cut = 0; cut < data.size(); ++cut) {
        bool rejected = false;
        try { hybrid::DxEngine bad(std::span(data.data(), cut)); } catch (...) { rejected = true; }
        require(rejected, "Truncated voice bank accepted");
    }
    auto invalid = data; invalid[16 + 126] = 127;
    bool rejected = false;
    try { hybrid::DxEngine bad(invalid); } catch (...) { rejected = true; }
    require(rejected, "Unsafe pitch-envelope value accepted");
    const auto small = render(96, 44100), large = render(512, 44100);
    require(small == large, "FM audio depends on host block size");
    require(std::all_of(small.begin(), small.end(), [](float x) { return std::isfinite(x); }), "Nonfinite FM audio");
    require(std::all_of(small.begin(), small.begin() + 101, [](float x) { return x == 0; }), "Note began too early");
    require(*std::max_element(small.begin(), small.end()) > 0.001f, "FM note silent");
    const auto at48 = render(96, 48000);
    auto crossings = [](const std::vector<float>& x, unsigned rate) {
        unsigned count = 0;
        for (unsigned i = rate / 4 + 1; i < rate * 3 / 4; ++i)
            if (x[i - 1] <= 0 && x[i] > 0) ++count;
        return count;
    };
    require(std::abs(static_cast<int>(crossings(small, 44100))
        - static_cast<int>(crossings(at48, 48000))) <= 1, "44.1/48 pitch changed");
    const auto octave = render(96, 44100, false, 12);
    require(std::abs(static_cast<int>(crossings(octave, 44100))
        - 2 * static_cast<int>(crossings(small, 44100))) <= 2, "RPN octave bend did not double pitch");
    render(512, 44100, true);
    documentedPartTests();
    routingTests();
    partEffectsTests();
    nativeControlTests();
    userBankTests();
    pluginVoiceTests();
    integrationSafetyTests();
    extendedPitchTests();
    if (argc == 2) sweep(argv[1]);
    std::cout << "DX: parser bounds, validation, bank commit, timed onset, block invariance, release, 44.1/48 pitch and reverb buses passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
