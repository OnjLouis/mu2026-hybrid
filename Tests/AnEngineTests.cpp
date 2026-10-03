#include "AnEngine.h"
#include "AnFreeEg.h"
#include "AnControlMatrix.h"
#include "AnFilter.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
std::vector<std::uint8_t> bank(unsigned mode = 0) {
    std::vector<std::uint8_t> bytes {'A','N','P','1',1,0,0,0,3,0,0,0};
    std::vector<std::uint8_t> p(300);
    std::copy_n("Synthetic ",10,p.begin());
    auto s = p.begin()+104;
    s[0] = mode; s[1] = 66; s[2] = 62; s[5] = 3; s[19] = 3;
    s[24] = s[25] = s[28] = s[33] = s[34] = s[37] = s[57] = s[64] = 64;
    s[27] = s[36] = 64; s[31] = s[40] = 0; s[30] = s[39] = 1;
    s[26] = s[35] = 127;
    s[41] = 127; s[42] = 0; s[46] = 50; s[47] = 127;
    s[51] = 127; s[52] = 25; s[53] = 1; s[54] = 0; s[56] = 64;
    s[59] = 50; s[60] = 127; s[61] = 30; s[63] = 105; s[117] = 64;
    bytes.insert(bytes.end(),p.begin(),p.end());
    for (unsigned msb : {36,84,100}) {
        bytes.push_back(msb); bytes.insert(bytes.end(),{0,0,0,0});
    }
    return bytes;
}
void setup(hybrid::AnEngine& e) { e.queueShort(0x24'00'b0,0); e.queueShort(0x00'20'b0,0); e.queueShort(0xc0,0); }
float peak(const std::vector<float>& v, unsigned start, unsigned count) {
    float p = 0; for (unsigned i = start; i < start+count; ++i) { require(std::isfinite(v[i]),"Nonfinite audio"); p = std::max(p,std::abs(v[i])); } return p;
}
std::vector<float> render(unsigned block, unsigned rate, const std::vector<std::uint8_t>& patch = bank()) {
    hybrid::AnEngine e(patch); e.setSampleRate(float(rate)); setup(e);
    e.queueShort(0x00'5b'b0,0); e.queueShort(0x64'45'90,101); e.queueShort(0x00'45'80,rate);
    std::vector<float> output(block*16), audio;
    for (unsigned f = 0; f < rate*2;) {
        auto n = std::min(block,rate*2-f); e.render(output.data(),block,n);
        require(peak(output,0,n) < 1,"Output headroom exceeded");
        audio.insert(audio.end(),output.begin(),output.begin()+n); f += n;
    }
    require(e.activeVoices() == 0,"Released voice remains active");
    require(peak(audio,0,101) == 0,"Event occurred before sample timestamp");
    require(peak(audio,101,rate-101) > 0.001f,"AN silent"); return audio;
}
void documentedFilterTests() {
    require(hybrid::anFilterDamping(102)<hybrid::anFilterDamping(90)
        && hybrid::anFilterDamping(90)<hybrid::anFilterDamping(80),
        "Upper documented AN resonance range is flattened");
    const auto tone = [](unsigned note, unsigned type, unsigned hpf, unsigned volume = 127, unsigned feedback = 0) {
        auto patch = bank(); auto s = patch.begin()+12+104;
        s[4]=s[20]=64; s[41]=0; s[42]=127; s[32]=5;
        s[50]=type; s[51]=type == 1 ? 54 : 127;
        s[49]=hpf; s[62]=feedback; s[63]=volume;
        hybrid::AnEngine e(patch); setup(e);
        e.queueShort(0x90 | (note<<8) | (100<<16),0);
        std::vector<float> out(16*44100); e.render(out.data(),44100,44100);
        return std::vector<float>(out.begin()+22050,out.begin()+44100);
    };
    const auto rms = [](const auto& a) {
        double sum = 0; for (const auto x : a) sum += x*x;
        return std::sqrt(sum/a.size());
    };
    const double low = rms(tone(45,2,84))/rms(tone(45,2,0));
    const double high = rms(tone(57,2,84))/rms(tone(57,2,0));
    require(high/low>1.7 && high/low<2.5,"Separate AN HPF must have a 6dB/octave slope");
    const double lp18low = rms(tone(93,1,0))/rms(tone(93,2,0));
    const double lp18high = rms(tone(105,1,0))/rms(tone(105,2,0));
    require(lp18high/lp18low<0.18,"LP18 is a blend, not an 18dB/octave response");
    const auto quiet = tone(57,2,0,16,127), loud = tone(57,2,0,127,127);
    double difference = 0;
    for (unsigned i=0;i<quiet.size();++i) difference += std::abs(loud[i]-quiet[i]*127/16);
    require(difference/quiet.size()>0.0001,"Feedback taken before VCA volume");
}
void documentedDistortionTests() {
    const auto tone = [](unsigned wet, unsigned cutoff) {
        auto patch = bank(); auto common = patch.begin()+12; auto s = common+104;
        common[29]=100; common[31]=0; common[33]=cutoff; common[35]=100;
        s[4]=s[20]=64; s[41]=0; s[42]=127; s[32]=5; s[66]=wet;
        hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'51'90,0);
        std::vector<float> out(16*8192); e.render(out.data(),8192,8192);
        out.resize(8192); return out;
    };
    const auto dry=tone(1,60), wet=tone(127,60), filtered=tone(127,34);
    require(dry!=wet,"Amp Type Off incorrectly bypasses distortion drive");
    require(wet!=filtered,"AN distortion LPF cutoff ignored");
    double dryPower=0, filteredPower=0;
    for (unsigned i=4096;i<8192;++i) {
        dryPower+=wet[i]*wet[i]; filteredPower+=filtered[i]*filtered[i];
    }
    require(filteredPower<dryPower*0.6,"AN distortion LPF fails to attenuate high-frequency tone");
    require(dry==tone(1,34),"Dry distortion mix must bypass wet LPF");
}
void oscillatorControlTests() {
    const auto tone = [](unsigned edge, unsigned syncPitchMod, unsigned wave = 0) {
        auto patch=bank(); auto s=patch.begin()+12+104;
        s[4]=s[20]=64; s[26]=edge; s[23]=wave;
        s[15]=1; s[16]=76; s[19]=syncPitchMod; s[121]=3;
        s[30]=1; s[31]=64; s[9]=0; s[10]=1; s[11]=32;
        hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'45'90,0);
        std::vector<float> out(16*8192); e.render(out.data(),8192,8192);
        out.resize(8192); return out;
    };
    require(tone(0,3)!=tone(127,3),"VCO Edge control ignored");
    require(tone(127,1)!=tone(127,2),"Sync master/slave pitch-modulation selector ignored");
    require(tone(127,3)!=tone(127,1),"Sync Both must modulate slave as well as master");
    const auto delayedNote = [](unsigned reset, unsigned start) {
        auto patch=bank(); auto s=patch.begin()+12+104;
        s[4]=s[20]=64; s[23]=5; s[8]=reset;
        s[30]=1; s[31]=64; s[10]=1; s[11]=32;
        hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'45'90,start);
        const auto length=start+4096;
        std::vector<float> out(16*length); e.render(out.data(),length,length);
        return std::vector<float>(out.begin()+start,out.begin()+length);
    };
    require(delayedNote(0,0)!=delayedNote(0,22050),"Free-running LFO incorrectly restarts on note-on");
    require(delayedNote(1,0)==delayedNote(1,22050),"Key-on LFO does not reset on note-on");
}
void stepSequencerTests() {
    auto patch=bank(); auto common=patch.begin()+12; auto s=common+104; auto sequence=s+122;
    common[17]=120; common[18]=127; common[80]=common[81]=common[83]=1;
    sequence[0]=7; sequence[1]=2; sequence[6]=72; sequence[7]=60;
    sequence[22]=100; sequence[23]=0; sequence[38]=64;
    common[89]=common[91]=100;
    s[4]=s[20]=64; s[23]=5; s[61]=0;
    hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'30'90,0);
    std::vector<float> out(16*16384); e.render(out.data(),16384,16384);
    require(peak(out,8000,2000)<0.000001f,"Stored sequencer rest ignored");
    require(peak(out,12000,2000)>0.001f,"Stored sequencer pattern failed to loop");
    auto directPatch=patch; directPatch[12+80]=0;
    hybrid::AnEngine direct(directPatch); setup(direct); direct.queueShort(0x64'48'90,0);
    std::vector<float> expected(16*4096); direct.render(expected.data(),4096,4096);
    require(std::equal(expected.begin(),expected.begin()+4096,out.begin()),"Sequencer did not play stored note at C2 base pitch");
    hybrid::AnEngine stopped(patch); setup(stopped); stopped.queueShort(0x64'30'90,0);
    stopped.queueShort(0x00'30'80,8192); stopped.render(out.data(),16384,16384);
    require(peak(out,12000,2000)==0,"Released trigger restarted sequencer");
    require(!stopped.heldNote(0,48),"Sequence trigger note remains held");
    {
        auto overlapPatch=patch;
        overlapPatch[12+226+23]=100; overlapPatch[12+226+38]=127;
        hybrid::AnEngine overlap(overlapPatch); setup(overlap); overlap.queueShort(0x64'30'90,0);
        overlap.render(out.data(),16384,5600);
        require(overlap.activeVoices()>=2,"Long sequencer gates do not overlap voices");
        overlap.queueShort(0x00'78'b0,5600); overlap.render(out.data(),16384,8192);
        require(overlap.activeVoices()==0,"Panic failed to stop sequencer and generated notes");
    }
    {
        auto heldPatch=patch; heldPatch[12+84]=1;
        hybrid::AnEngine held(heldPatch); setup(held); held.queueShort(0x64'30'90,0);
        held.queueShort(0x00'30'80,8192); held.render(out.data(),16384,16384);
        require(peak(out,12000,2000)>0.001f,"Sequencer Hold did not retain pattern");
        held.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,0,0,0x7e,0,0xf7},16384);
        held.render(out.data(),16384,16384);
        require(held.activeVoices()==0 && peak(out,0,16384)==0,"Reset did not stop held pattern");
    }
    require(render(96,44100,patch)==render(512,44100,patch),"Sequencer depends on host block size");
    render(96,48000,patch);
}
int main(int argc, char** argv) {
    try {
        if (argc == 2) {
            for (unsigned preset = 0; preset < 256; ++preset) {
                hybrid::AnEngine actual {std::filesystem::path(argv[1])};
                require(actual.selections()==848,"Actual bank count wrong");
                actual.queueShort(0x24'00'b0,0);
                actual.queueShort(0xb0 | (32<<8) | ((preset/128)<<16),0);
                actual.queueShort(0xc0 | ((preset%128)<<8),0);
                actual.queueShort(0x64'3c'90,0);
                std::vector<float> out(16*4096); actual.render(out.data(),4096,4096);
                require(peak(out,0,4096)<1,"Actual preset exceeded dry headroom");
            }
            std::cout << "All 256 actual AN presets produce finite bounded output\n"; return 0;
        }
        documentedFilterTests();
        documentedDistortionTests();
        oscillatorControlTests();
        stepSequencerTests();
        require(render(96,44100) == render(512,44100),"Block size changed audio");
        {
            auto patch=bank();
            patch[12+104+4]=patch[12+104+20]=64;
            patch[12+104+27]=64;
            hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'45'90,0);
            std::vector<float> out(16*44100); e.render(out.data(),44100,44100);
            auto power=[&](double frequency) {
                double real=0,imag=0;
                for (unsigned i=11025;i<33075;++i) {
                    const double phase=frequency*i*6.283185307179586/44100;
                    real+=out[i]*std::cos(phase); imag+=out[i]*std::sin(phase);
                }
                return real*real+imag*imag;
            };
            require(power(880)>power(440)*4,"Reference-tested AN saw pitch law regressed");
            patch[12+104+23]=2;
            hybrid::AnEngine saw2(patch); setup(saw2); saw2.queueShort(0x64'45'90,0);
            saw2.render(out.data(),44100,44100);
            require(power(440)>power(880),"Saw2 lost fundamental under PWM");
        }
        {
            auto sineBank = bank(); auto triangleBank = sineBank;
            sineBank[12+104+30]=1; sineBank[12+104+31]=32;
            triangleBank=sineBank; triangleBank[12+104+9]=5;
            hybrid::AnEngine sine(sineBank), triangle(triangleBank); setup(sine); setup(triangle);
            sine.queueShort(0x64'45'90,0); triangle.queueShort(0x64'45'90,0);
            std::vector<float> a(16*4096), b(16*4096);
            sine.render(a.data(),4096,4096); triangle.render(b.data(),4096,4096);
            require(a!=b,"AN LFO waveform ignored");
            triangleBank[12+104+9]=17;
            require(render(96,44100,triangleBank)==render(512,44100,triangleBank),"Random LFO changed with host block size");
        }
        {
            std::array<std::uint8_t,104> common {};
            common[94] = 50;
            require(hybrid::anEgDuration(common)==5,"Free EG seconds decoded incorrectly");
            common[94] = 5; common[17] = 120;
            require(hybrid::anEgDuration(common)==4,"Free EG bars decoded incorrectly");
            require(hybrid::anEgPosition(1.25,1)==0.25,"Free EG forward loop wrong");
            require(hybrid::anEgPosition(1.25,2)==0.75,"Free EG half loop wrong");
            require(hybrid::anEgPosition(1.25,3)==0.75,"Free EG alternate loop wrong");
            require(hybrid::anEgPosition(1.25,4)==0.75,"Free EG alternate half loop wrong");
            require(hybrid::anEgPosition(3,0)==1,"Free EG one-shot failed to retain endpoint");
            std::array<std::uint8_t,122> base {};
            base[10]=1; base[11]=10;
            std::array<std::uint8_t,768> data; data.fill(128);
            common[96]=6; common[97]=5;
            require(hybrid::anEgScene(base,common,data,1)==base,"Neutral Free EG changed scene");
            data.fill(148);
            auto scene=hybrid::anEgScene(base,common,data,1);
            require(scene[10]==1 && scene[11]==30,"Free EG split multi-byte parameter");
            common[97]=0;
            require(hybrid::anEgScene(base,common,data,1)==base,"Disabled Free EG track modified scene");
        }
        {
            auto plainBank = bank();
            auto movingBank = plainBank;
            movingBank[12+296] = 0; movingBank[12+297] = 3;
            movingBank[12+93] = 0; movingBank[12+94] = 10;
            movingBank[12+96] = 18; movingBank[12+97] = 1;
            std::vector<std::uint8_t> eg(768,128);
            std::fill_n(eg.begin(),192,140);
            movingBank.insert(movingBank.begin()+312,eg.begin(),eg.end());
            hybrid::AnEngine plain(plainBank), moving(movingBank);
            setup(plain); setup(moving);
            plain.queueShort(0x64'45'90,0); moving.queueShort(0x64'45'90,0);
            std::vector<float> a(16*4096), b(16*4096);
            plain.render(a.data(),4096,4096); moving.render(b.data(),4096,4096);
            require(a!=b,"Recorded Free EG pitch movement ignored");
            require(render(96,44100,movingBank)==render(512,44100,movingBank),"Free EG changed with host block size");
            render(96,48000,movingBank);
        }
        render(96,48000); render(96,96000);
        {
            auto patch=bank(); auto s=patch.begin()+12+104;
            s[4]=s[20]=s[55]=64; s[51]=50; s[52]=25;
            s[53]=1; s[54]=60; s[46]=60; s[47]=0;
            hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'2d'90,0);
            std::vector<float> out(16*44100); e.render(out.data(),44100,44100);
            auto power=[&](unsigned start, double hz) {
                double real=0,imag=0;
                for (unsigned i=0;i<4096;++i) {
                    const double window=0.5-0.5*std::cos(i*6.283185307179586/4095);
                    const double phase=hz*(start+i)*6.283185307179586/44100;
                    real+=out[start+i]*window*std::cos(phase);
                    imag+=out[start+i]*window*std::sin(phase);
                }
                return real*real+imag*imag;
            };
            const auto attackBrightness=power(441,880)/power(441,220);
            const auto sustainBrightness=power(22050,880)/power(22050,220);
            require(attackBrightness>sustainBrightness*4,"Filter envelope failed to close harmonic bandwidth");
            require(render(96,44100,patch)==render(512,44100,patch),"Filter envelope depends on host block size");
            render(96,48000,patch);
        }
        {
            auto patch = bank();
            auto s = patch.begin()+12+104;
            s[4]=s[20]=64; s[55]=64; s[51]=50; s[47]=0;
            s[53]=1; s[54]=72; s[46]=90;
            auto dry = [&](const std::vector<std::uint8_t>& data, unsigned velocity) {
                hybrid::AnEngine e(data); setup(e);
                e.queueShort(0x90 | (45<<8) | (velocity<<16),0);
                std::vector<float> out(16*8192); e.render(out.data(),8192,8192);
                out.resize(8192); return out;
            };
            require(dry(patch,30)==dry(patch,120),"Neutral velocity sensitivity changed filter");
            s[55]=100;
            require(dry(patch,30)!=dry(patch,120),"Filter EG velocity sensitivity ignored");
            s[55]=64; s[68]=99; s[69]=30; s[70]=88;
            require(dry(patch,30)!=dry(patch,120),"Velocity matrix source ignored by filter decay");
            s[68]=98;
            const auto tracked=dry(patch,100);
            s[68]=0;
            require(tracked!=dry(patch,100),"Key-track matrix source ignored by filter decay");
        }
        {
            auto dry = [&](const std::vector<std::uint8_t>& patch, unsigned cc = 0, unsigned value = 0) {
                hybrid::AnEngine e(patch); setup(e);
                if (cc) e.queueShort(0xb0 | (cc<<8) | (value<<16),0);
                e.queueShort(0x64'39'90,0);
                std::vector<float> out(16*8192); e.render(out.data(),8192,8192);
                out.resize(8192); return out;
            };
            for (unsigned target : {15u,16u,17u,21u,22u,23u,33u,42u}) {
                auto patch=bank(); auto s=patch.begin()+12+104;
                s[4]=s[20]=64; s[23]=s[32]=1;
                if (target == 15) s[26]=64;
                if (target == 21) s[35]=64;
                s[28]=s[37]=90; s[29]=s[38]=3;
                s[42]=90; s[49]=50; s[62]=80;
                s[68]=107; s[69]=target; s[70]=100;
                require(dry(patch)!=dry(patch,41,127),"AN oscillator/HPF/feedback matrix target ignored");
            }
            {
                auto patch=bank(); auto s=patch.begin()+12+104;
                s[4]=64; s[20]=110; s[23]=s[32]=1;
                s[21]=2; s[47]=0; s[46]=40;
                const auto envelope=dry(patch);
                s[21]=0;
                require(envelope!=dry(patch),"FM depth envelope source ignored");
                const auto oscillator=dry(patch);
                s[22]=4;
                require(oscillator!=dry(patch),"FM modulation wave source ignored");
            }
            {
                auto patch=bank(); auto s=patch.begin()+12+104;
                s[4]=s[20]=64; s[41]=0; s[42]=127;
                s[23]=1; s[32]=5; s[37]=64; s[38]=0;
                const auto plain=dry(patch);
                s[37]=100;
                require(plain!=dry(patch),"Sine-wave cross modulation treated as inaudible pulse width");
                const auto fixed=dry(patch);
                s[38]=2; s[47]=0; s[46]=40;
                require(fixed!=dry(patch),"Cross-modulation envelope source ignored");
            }
        }
        {
            std::array<std::uint8_t,122> scene {};
            hybrid::anDirectControl(scene,7,127);
            require(scene[10]==1 && scene[11]==127,"Direct control split 8-bit LFO parameter");
            hybrid::anDirectControl(scene,14,0);
            require(scene[25]==14,"Direct fine-tune minimum wrong");
            hybrid::anDirectControl(scene,14,127);
            require(scene[25]==114,"Direct fine-tune maximum wrong");
            auto patch=bank(); auto s=patch.begin()+12+104;
            s[4]=s[20]=64; s[23]=1;
            s[68]=107; s[69]=16; s[70]=64;
            auto dry=[&](unsigned value, bool send) {
                hybrid::AnEngine e(patch); setup(e);
                if (send) e.queueShort(0xb0 | (41<<8) | (value<<16),0);
                e.queueShort(0x64'39'90,0);
                std::vector<float> out(16*4096); e.render(out.data(),4096,4096);
                out.resize(4096); return out;
            };
            require(dry(64,true)==dry(0,false),"Unreceived direct knob overwrote factory parameter");
            require(dry(10,true)!=dry(100,true),"Direct control depth treated as zero modulation");
            require(render(96,48000,patch)==render(512,48000,patch),"Direct-control bank changed with block size");
        }
        auto data = bank();
        for (unsigned n = 0; n < data.size(); ++n) {
            bool rejected = false; try { hybrid::AnEngine e(std::span(data.data(),n)); } catch (...) { rejected = true; }
            require(rejected,"Truncated bank accepted");
        }
        for (unsigned target = 0; target < 4; ++target) {
            hybrid::AnEngine e(bank()); setup(e); e.queueShort(0x64'45'90,0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,3,std::uint8_t(target),0,0x47,0,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,3,std::uint8_t(target),12,0,0xf7},0);
            std::vector<float> output(16*1024); e.render(output.data(),1024,1024);
            require(peak(output,0,1024)==0,"Insertion duplicated dry voice");
            require(peak(output,(8+target*2)*1024,1024)>0.001f,"Insertion bus silent");
        }
        {
            hybrid::AnEngine e(bank()); setup(e); e.queueShort(0x64'45'90,0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,3,0,0,0x47,0,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,3,0,12,0,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,2,1,0x5a,1,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,2,1,0x40,0,0,0xf7},0);
            std::vector<float> out(16*1024); e.render(out.data(),1024,1024);
            require(peak(out,8*1024,1024)>0.001f,"System variation overwrote native insertion 1");
            require(peak(out,0,1024)==0,"Native insertion voice leaked into dry bus");
        }
        {
            hybrid::AnEngine e(bank()); setup(e); e.queueShort(0x64'45'90,0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,2,1,0x40,0x5d,0,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,2,1,0x5b,0,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,3,0,0,5,0,0xf7},0);
            e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,3,0,12,1,0xf7},0);
            std::vector<float> out(16*1024); e.render(out.data(),1024,1024);
            require(peak(out,6*1024,1024)>0.001f,"Native insertion 1 stole AN variation input");
            require(peak(out,0,1024)==0,"Insertion variation leaked dry AN input");
        }
        {
            auto patch = bank(); patch[12+104+23] = 5; patch[12+104+20] = 64; patch[12+104+4] = 64;
            patch[12+104+68] = 107; patch[12+104+69] = 13; patch[12+104+70] = 76;
            hybrid::AnEngine plain(patch), moved(patch); setup(plain); setup(moved);
            plain.queueShort(0x64'45'90,0); moved.queueShort(0x7f'29'b0,0); moved.queueShort(0x64'45'90,0);
            std::vector<float> a(16*4096), b(16*4096);
            plain.render(a.data(),4096,4096); moved.render(b.data(),4096,4096);
            require(a!=b,"Assignable AN knob 1 did not receive default CC41");
        }
        {
            auto patch = bank(); patch[12+104+23] = 5; patch[12+104+20] = 64; patch[12+104+4] = 64;
            hybrid::AnEngine plain(patch), boosted(patch); setup(plain); setup(boosted);
            boosted.queueShort(0x01'63'b0,0); boosted.queueShort(0x30'62'b0,0); boosted.queueShort(0x54'06'b0,0);
            plain.queueShort(0x64'21'90,0); boosted.queueShort(0x64'21'90,0);
            std::vector<float> a(16*44100), b(16*44100);
            plain.render(a.data(),44100,44100); boosted.render(b.data(),44100,44100);
            require(peak(b,11025,22050)>peak(a,11025,22050)*1.5f,"NRPN bass EQ did not reach AN voice");
        }
        {
            auto patch = bank(); patch[12+104+15] = 1; patch[12+104+16] = 71;
            patch[12+104+20] = patch[12+104+4] = 64; patch[12+104+23] = 0;
            patch[12+104+27]=0;
            hybrid::AnEngine e(patch); setup(e); e.queueShort(0x64'45'90,0);
            std::vector<float> out(16*44100); e.render(out.data(),44100,44100);
            auto power = [&](double frequency) {
                double real=0, imag=0;
                for (unsigned i=11025;i<33075;++i) {
                    const double phase = frequency*i*6.283185307179586/44100;
                    real+=out[i]*std::cos(phase); imag+=out[i]*std::sin(phase);
                }
                return real*real+imag*imag;
            };
            require(power(440)>power(440*std::exp2(7.0/12))*4,"Sync Pitch retuned master instead of slave oscillator");
        }
        {
            hybrid::AnEngine e(bank()); setup(e); e.queueShort(0x7f'5b'b0,0); e.queueShort(0x7f'5d'b0,0); e.queueShort(0x64'45'90,0);
            std::vector<float> output(16*1024); e.render(output.data(),1024,1024);
            require(peak(output,2*1024,1024)>0.001f && peak(output,4*1024,1024)>0.001f,"Effects sends silent");
            e.queueShort(0x00'c0,1024); e.queueShort(0x00'45'80,1024); e.render(output.data(),1024,1024);
            e.queueShort(0x00'78'b0,2048); e.render(output.data(),1024,1024); require(e.activeVoices()==0,"Panic did not clear voices");
        }
        {
            hybrid::AnEngine e(bank());
            e.queueShort(0x64'00'b0,0); e.queueShort(0x01'c0,0);
            require(!e.selected(0),"Empty MSB100 must fall back to MU");
            require(!e.queueShort(0x64'45'90,0),"Empty MSB100 swallowed MU note");
            setup(e); require(e.selected(0),"Native AN selection not recognized");
            e.queueShort(0x64'45'90,0); e.queueSysex(std::vector<std::uint8_t>{0xf0,0x43,0x10,0x4c,0,0,0x7e,0,0xf7},0);
            std::vector<float> out(16*256); e.render(out.data(),256,256); require(e.activeVoices()==0,"Reset did not clear voices");
            require(!e.selected(0),"Reset did not clear routing");
        }
        {
            hybrid::AnEngine e(bank(2)); setup(e); e.queueShort(0x64'3c'90,0); e.queueShort(0x64'40'90,50); e.queueShort(0x00'40'80,100);
            std::vector<float> out(16*256); e.render(out.data(),256,256); require(e.activeVoices()==1,"Legato stack lost held note");
        }
        {
            hybrid::AnEngine e(bank()); setup(e);
            for (unsigned note=60; note<70; ++note) e.queueShort(0x90 | (note<<8) | (100<<16),0);
            std::vector<float> out(16*256); e.render(out.data(),256,256);
            require(e.activeVoices()==5,"AN part exceeded five notes");
            e.queueShort(0x7f'40'b0,256); e.queueShort(0x00'7b'b0,256); e.render(out.data(),256,256);
            require(e.activeVoices()==5,"Sustain lost held voices");
            e.queueShort(0x00'40'b0,512);
            for (unsigned f=0; f<100; ++f) e.render(out.data(),256,256);
            require(e.activeVoices()==0,"Sustain release left stuck voices");
        }
        {
            auto patch = bank(); patch[12+104+23] = 5; patch[12+104+20] = 64; patch[12+104+4] = 64;
            for (unsigned rate : {44100,48000}) {
                hybrid::AnEngine e(patch); setup(e); e.setSampleRate(float(rate)); e.queueShort(0x64'45'90,0);
                std::vector<float> out(16*rate); e.render(out.data(),rate,rate);
                unsigned crossings=0;
                for (unsigned i=rate/4+1; i<rate*3/4; ++i) if (out[i-1]<0 && out[i]>=0) ++crossings;
                require(crossings>=218 && crossings<=222,"Sample rate changed concert-A pitch");
                e.queueShort(0x7f'7f'e0,rate); e.render(out.data(),rate,rate); crossings=0;
                for (unsigned i=rate/4+1; i<rate*3/4; ++i) if (out[i-1]<0 && out[i]>=0) ++crossings;
                require(crossings>=245 && crossings<=249,"Pitch bend range not applied");
            }
        }
        {
            hybrid::AnEngine e(bank()); setup(e); e.queueShort(0x64'45'90,0);
            require(e.heldNote(0,69),"AN note ownership not recorded");
            e.queueShort(0x00'00'b0,1); e.queueShort(0xc0,1);
            require(e.heldNote(0,69),"Bank change erased old note ownership");
            e.queueShort(0x00'45'80,2); require(!e.heldNote(0,69),"Note ownership did not release");
        }
        std::cout << "AN engine parsing, timing, ownership, rates, mono, reset, sends and four insertions pass\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
