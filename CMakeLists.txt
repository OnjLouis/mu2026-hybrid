cmake_minimum_required(VERSION 3.25)

project(Mu2026Hybrid VERSION 0.2.1 LANGUAGES CXX)

if(NOT WIN32 OR CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Mu2026 Hybrid currently requires a 32-bit Windows target")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(DxFm STATIC
    Source/DxEngine.cpp Source/MidiSystemReset.cpp Source/XgVariationRouting.cpp
    ThirdParty/msfa/dx7note.cc ThirdParty/msfa/env.cc ThirdParty/msfa/exp2.cc
    ThirdParty/msfa/fm_core.cc ThirdParty/msfa/fm_op_kernel.cc
    ThirdParty/msfa/freqlut.cc ThirdParty/msfa/lfo.cc ThirdParty/msfa/log2.cc
    ThirdParty/msfa/patch.cc ThirdParty/msfa/pitchenv.cc ThirdParty/msfa/sin.cc)
target_include_directories(DxFm PUBLIC Source PRIVATE ThirdParty/msfa)
target_compile_options(DxFm PRIVATE -fwrapv)
target_compile_definitions(DxFm PRIVATE _USE_MATH_DEFINES)

add_library(AnSynth STATIC Source/AnEngine.cpp Source/MidiSystemReset.cpp Source/XgVariationRouting.cpp)
target_include_directories(AnSynth PUBLIC Source)
add_executable(AnEngineTests Tests/AnEngineTests.cpp)
target_link_libraries(AnEngineTests PRIVATE AnSynth)
target_link_options(AnEngineTests PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(DxEngineTests Tests/DxEngineTests.cpp)
target_link_libraries(DxEngineTests PRIVATE DxFm)
target_link_options(DxEngineTests PRIVATE -static -static-libgcc -static-libstdc++)

add_library(Mu2026Hybrid SHARED
    Source/HybridEditor.cpp
    Source/HybridEditor.h
    Source/HybridStatus.cpp
    Source/HybridStatus.h
    Source/GsEffectTranslator.cpp
    Source/GsEffectTranslator.h
    Source/GsPartPitch.cpp
    Source/GsPartPitch.h
    Source/MidiRouter.cpp
    Source/MidiRouter.h
    Source/MidiSystemReset.cpp
    Source/MidiSystemReset.h
    Source/MuVoiceMapSelector.cpp
    Source/MuVoiceMapSelector.h
    Source/MuVoiceCatalog.cpp
    Source/MuVoiceCatalog.h
    Source/MuInsertionRouting.h
    Source/NativeSgClient.cpp
    Source/NativeSgClient.h
    Source/NativeVlClient.cpp
    Source/NativeVlClient.h
    Source/NativeVlProtocol.h
    Source/NativeWorkerPreloader.cpp
    Source/NativeWorkerPreloader.h
    Source/OrderedSetupHistory.h
    Source/SgRouting.cpp
    Source/SgRouting.h
    Source/StreamingRateAdapter.cpp
    Source/StreamingRateAdapter.h
    Source/VlPartRouter.cpp
    Source/VlPartRouter.h
    Source/VlPluginVoiceBulk.h
    Source/XgPartModes.cpp
    Source/XgPartModes.h
    Source/XgVariationRouting.cpp
    Source/XgVariationRouting.h
    Source/XglEngine.cpp
    Source/XglEngine.h
    Source/XglResetControllers.h
    Source/XglVoiceMap.cpp
    Source/XglVoiceMap.h
    Source/HybridVst.cpp
    Source/Vst2Abi.h
    Source/exports.def
)
set_target_properties(Mu2026Hybrid PROPERTIES
    OUTPUT_NAME "mu2026-hybrid"
    PREFIX ""
)
target_compile_definitions(Mu2026Hybrid PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(Mu2026Hybrid PRIVATE
    comctl32 user32 gdi32 oleacc ole32 uuid DxFm AnSynth
)
target_link_options(Mu2026Hybrid PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(Mu2026VlWorker
    NativeWorker/VlNativeWorker.cpp
    Source/LeImageLoader.cpp
    Source/LeImageLoader.h
    Source/NativeVlEngine.cpp
    Source/NativeVlEngine.h
    Source/NativeVlProtocol.h
)
set_target_properties(Mu2026VlWorker PROPERTIES
    OUTPUT_NAME "mu2026-vl-worker"
)
target_compile_definitions(Mu2026VlWorker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(Mu2026VlWorker PRIVATE
    -municode -static -static-libgcc "-static-libstdc++"
)

add_executable(Mu2026SgWorker
    NativeWorker/SgNativeWorker.cpp
    Source/LeImageLoader.cpp
    Source/LeImageLoader.h
    Source/NativeSgEngine.cpp
    Source/NativeSgEngine.h
    Source/NativeVlProtocol.h
)
set_target_properties(Mu2026SgWorker PROPERTIES
    OUTPUT_NAME "mu2026-sg-worker"
)
target_compile_definitions(Mu2026SgWorker PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(Mu2026SgWorker PRIVATE
    -municode -static -static-libgcc "-static-libstdc++"
)

add_executable(HybridHostProbe
    Tests/HybridHostProbe.cpp
    Source/Vst2Abi.h
)
target_compile_definitions(HybridHostProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(HybridHostProbe PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(HybridTimedProbe
    Tests/HybridTimedProbe.cpp
    Source/Vst2Abi.h
)
target_compile_definitions(HybridTimedProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(HybridTimedProbe PRIVATE -static -static-libgcc -static-libstdc++)

add_executable(HybridXgForwardingProbe
    Tests/HybridXgForwardingProbe.cpp
    Source/Vst2Abi.h
)
target_compile_definitions(HybridXgForwardingProbe PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_options(HybridXgForwardingProbe PRIVATE
    -static -static-libgcc -static-libstdc++
)

add_executable(HybridDualInstanceProbe
    Tests/HybridDualInstanceProbe.cpp
    Source/Vst2Abi.h
)
target_compile_definitions(HybridDualInstanceProbe PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_options(HybridDualInstanceProbe PRIVATE
    -static -static-libgcc -static-libstdc++
)

add_executable(HybridEditorHostProbe
    Tests/HybridEditorHostProbe.cpp
    Source/Vst2Abi.h
)
target_include_directories(HybridEditorHostProbe PRIVATE Source)
target_compile_definitions(HybridEditorHostProbe PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(HybridEditorHostProbe PRIVATE comctl32 user32 gdi32)
target_link_options(HybridEditorHostProbe PRIVATE
    -municode -static -static-libgcc -static-libstdc++
)

enable_testing()
add_test(NAME DxEngineTests COMMAND DxEngineTests)

add_library(MockMuEngine SHARED Tests/MockMuEngine.cpp)
target_link_options(MockMuEngine PRIVATE -static -static-libgcc -static-libstdc++)
add_executable(HybridFirmwareModeProbe Tests/HybridFirmwareModeProbe.cpp)
target_compile_definitions(HybridFirmwareModeProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(HybridFirmwareModeProbe PRIVATE -static -static-libgcc -static-libstdc++)
add_test(NAME HybridFirmwareModeProbe COMMAND HybridFirmwareModeProbe
    $<TARGET_FILE:Mu2026Hybrid> $<TARGET_FILE:MockMuEngine>)

add_executable(MuVoiceCatalogTests
    Tests/MuVoiceCatalogTests.cpp
    Source/MuVoiceCatalog.cpp
    Source/XglVoiceMap.cpp
)
target_link_options(MuVoiceCatalogTests PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(MuFallbackRoutingTests
    Tests/MuFallbackRoutingTests.cpp
    Source/MuVoiceCatalog.cpp
    Source/MuVoiceMapSelector.cpp
    Source/XglEngine.cpp
    Source/GsPartPitch.cpp
    Source/XglVoiceMap.cpp
    Source/XgPartModes.cpp
    Source/XgVariationRouting.cpp
)
target_link_options(MuFallbackRoutingTests PRIVATE
    -static -static-libgcc -static-libstdc++)

add_executable(MuInsertionRoutingTests Tests/MuInsertionRoutingTests.cpp)
target_link_options(MuInsertionRoutingTests PRIVATE
    -static -static-libgcc -static-libstdc++)
add_test(NAME MuInsertionRoutingTests COMMAND MuInsertionRoutingTests)

add_executable(NativeSgStartupTests Tests/NativeSgStartupTests.cpp)
target_include_directories(NativeSgStartupTests PRIVATE Source)
target_link_options(NativeSgStartupTests PRIVATE -static -static-libgcc -static-libstdc++)
add_test(NAME NativeSgStartupTests COMMAND NativeSgStartupTests)
add_executable(MidiRouterTests
    Tests/MidiRouterTests.cpp
    Source/MidiRouter.cpp
    Source/MidiRouter.h
)
target_include_directories(MidiRouterTests PRIVATE Source)
target_link_options(MidiRouterTests PRIVATE -static -static-libgcc -static-libstdc++)
add_test(NAME MidiRouterTests COMMAND MidiRouterTests)

add_executable(MidiSystemResetTests
    Tests/MidiSystemResetTests.cpp
    Source/MidiSystemReset.cpp
    Source/MidiSystemReset.h
)
target_include_directories(MidiSystemResetTests PRIVATE Source)
target_link_options(MidiSystemResetTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME MidiSystemResetTests COMMAND MidiSystemResetTests)

add_executable(XglResetControllersTests
    Tests/XglResetControllersTests.cpp
    Source/XglResetControllers.h
)
target_include_directories(XglResetControllersTests PRIVATE Source)
target_link_options(XglResetControllersTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME XglResetControllersTests COMMAND XglResetControllersTests)

add_executable(LeImageLoaderTests
    Tests/LeImageLoaderTests.cpp
    Source/LeImageLoader.cpp
    Source/LeImageLoader.h
)
target_include_directories(LeImageLoaderTests PRIVATE Source)
target_link_options(LeImageLoaderTests PRIVATE -static -static-libgcc -static-libstdc++)
add_test(NAME LeImageLoaderTests COMMAND LeImageLoaderTests)

add_executable(VlPartRouterTests
    Tests/VlPartRouterTests.cpp
    Source/VlPartRouter.cpp
    Source/VlPartRouter.h
)
target_include_directories(VlPartRouterTests PRIVATE Source)
target_link_options(VlPartRouterTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME VlPartRouterTests COMMAND VlPartRouterTests)

add_executable(VlPluginVoiceBulkTests
    Tests/VlPluginVoiceBulkTests.cpp
    Source/VlPluginVoiceBulk.h
)
target_include_directories(VlPluginVoiceBulkTests PRIVATE Source)
target_link_options(VlPluginVoiceBulkTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
target_compile_options(VlPluginVoiceBulkTests PRIVATE -UNDEBUG)
add_test(NAME VlPluginVoiceBulkTests COMMAND VlPluginVoiceBulkTests)

add_executable(SgRoutingTests
    Tests/SgRoutingTests.cpp
    Source/SgRouting.cpp
    Source/SgRouting.h
)
target_include_directories(SgRoutingTests PRIVATE Source)
target_link_options(SgRoutingTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME SgRoutingTests COMMAND SgRoutingTests)

add_executable(NativeEventTimelineTests
    Tests/NativeEventTimelineTests.cpp
    Source/NativeEventTimeline.h
)
target_include_directories(NativeEventTimelineTests PRIVATE Source)
target_link_options(NativeEventTimelineTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME NativeEventTimelineTests COMMAND NativeEventTimelineTests)

add_executable(OrderedMidiHistoryTests
    Tests/OrderedSetupHistoryTests.cpp
    Source/OrderedSetupHistory.h
)
target_include_directories(OrderedMidiHistoryTests PRIVATE Source)
target_link_options(OrderedMidiHistoryTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME OrderedMidiHistoryTests COMMAND OrderedMidiHistoryTests)

add_executable(MidiChannelSnapshotTests
    Tests/MidiChannelSnapshotTests.cpp
    Source/MidiChannelSnapshot.h
)
target_include_directories(MidiChannelSnapshotTests PRIVATE Source)
target_link_options(MidiChannelSnapshotTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME MidiChannelSnapshotTests COMMAND MidiChannelSnapshotTests)

add_executable(VlVoiceAllocatorTests
    Tests/VlVoiceAllocatorTests.cpp
    Source/VlVoiceAllocator.h
)
target_include_directories(VlVoiceAllocatorTests PRIVATE Source)
target_link_options(VlVoiceAllocatorTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME VlVoiceAllocatorTests COMMAND VlVoiceAllocatorTests)

# These legacy tests use the C assert macro. Keep their checks active when the
# normal release build is used for packaging and validation.
foreach(assertion_test IN ITEMS
    MidiChannelSnapshotTests
    VlPartRouterTests
    VlVoiceAllocatorTests
)
    target_compile_options(${assertion_test} PRIVATE -UNDEBUG)
endforeach()

add_executable(StreamingRateAdapterTests
    Tests/StreamingRateAdapterTests.cpp
    Source/StreamingRateAdapter.cpp
    Source/StreamingRateAdapter.h
)
target_include_directories(StreamingRateAdapterTests PRIVATE Source)
target_link_options(StreamingRateAdapterTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME StreamingRateAdapterTests COMMAND StreamingRateAdapterTests)

add_executable(MuVoiceMapSelectorTests
    Tests/MuVoiceMapSelectorTests.cpp
    Source/MuVoiceMapSelector.cpp
    Source/MuVoiceMapSelector.h
)
target_include_directories(MuVoiceMapSelectorTests PRIVATE Source)
target_link_options(MuVoiceMapSelectorTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME MuVoiceMapSelectorTests COMMAND MuVoiceMapSelectorTests)

add_executable(XglVoiceMapTests
    Tests/XglVoiceMapTests.cpp
    Source/XglVoiceMap.cpp
    Source/XglVoiceMap.h
)
target_include_directories(XglVoiceMapTests PRIVATE Source)
target_link_options(XglVoiceMapTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME XglVoiceMapTests COMMAND XglVoiceMapTests)

add_executable(XgPartModesTests
    Tests/XgPartModesTests.cpp
    Source/MidiSystemReset.cpp
    Source/MidiSystemReset.h
    Source/XgPartModes.cpp
    Source/XgPartModes.h
)
target_include_directories(XgPartModesTests PRIVATE Source)
target_link_options(XgPartModesTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME XgPartModesTests COMMAND XgPartModesTests)

add_executable(XgVariationRoutingTests
    Tests/XgVariationRoutingTests.cpp
    Source/XgVariationRouting.cpp
    Source/XgVariationRouting.h
)
target_include_directories(XgVariationRoutingTests PRIVATE Source)
target_link_options(XgVariationRoutingTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME XgVariationRoutingTests COMMAND XgVariationRoutingTests)

add_executable(GsEffectTranslatorTests
    Tests/GsEffectTranslatorTests.cpp
    Source/GsEffectTranslator.cpp
    Source/GsEffectTranslator.h
)
target_include_directories(GsEffectTranslatorTests PRIVATE Source)
target_link_options(GsEffectTranslatorTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME GsEffectTranslatorTests COMMAND GsEffectTranslatorTests)

add_executable(GsPartPitchTests
    Tests/GsPartPitchTests.cpp
    Source/GsPartPitch.cpp
    Source/GsPartPitch.h
)
target_include_directories(GsPartPitchTests PRIVATE Source)
target_link_options(GsPartPitchTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME GsPartPitchTests COMMAND GsPartPitchTests)

add_executable(HybridStatusTests
    Tests/HybridStatusTests.cpp
    Source/HybridStatus.cpp
    Source/HybridStatus.h
)
target_include_directories(HybridStatusTests PRIVATE Source)
target_link_options(HybridStatusTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME HybridStatusTests COMMAND HybridStatusTests)

add_executable(HybridEditorTests
    Tests/HybridEditorTests.cpp
    Source/HybridEditor.cpp
    Source/HybridEditor.h
    Source/HybridStatus.cpp
    Source/HybridStatus.h
    Source/Vst2Abi.h
)
target_include_directories(HybridEditorTests PRIVATE Source)
target_compile_definitions(HybridEditorTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(HybridEditorTests PRIVATE
    comctl32 user32 gdi32 oleacc oleaut32 uuid
)
target_link_options(HybridEditorTests PRIVATE
    -static -static-libgcc -static-libstdc++
)
add_test(NAME HybridEditorTests COMMAND HybridEditorTests)
target_compile_options(HybridStatusTests PRIVATE -UNDEBUG)
target_compile_options(HybridEditorTests PRIVATE -UNDEBUG)

add_executable(NativeWorkerStub
    Tests/NativeWorkerStub.cpp
    Source/NativeVlProtocol.h
)
target_include_directories(NativeWorkerStub PRIVATE Source)
target_compile_definitions(NativeWorkerStub PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_options(NativeWorkerStub PRIVATE
    -municode -static -static-libgcc -static-libstdc++
)

add_executable(NativeWorkerPreloaderTests
    Tests/NativeWorkerPreloaderTests.cpp
    Source/NativeSgClient.cpp
    Source/NativeSgClient.h
    Source/NativeVlClient.cpp
    Source/NativeVlClient.h
    Source/NativeVlProtocol.h
    Source/NativeWorkerPreloader.cpp
    Source/NativeWorkerPreloader.h
)
target_include_directories(NativeWorkerPreloaderTests PRIVATE Source)
target_compile_definitions(NativeWorkerPreloaderTests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_options(NativeWorkerPreloaderTests PRIVATE
    -municode -static -static-libgcc -static-libstdc++
)
target_compile_options(NativeWorkerPreloaderTests PRIVATE -UNDEBUG)
add_dependencies(NativeWorkerPreloaderTests NativeWorkerStub)
add_test(NAME NativeWorkerPreloaderTests COMMAND NativeWorkerPreloaderTests)

add_executable(HybridControllerSetupProbe Tests/HybridControllerSetupProbe.cpp)
set_target_properties(HybridControllerSetupProbe PROPERTIES OUTPUT_NAME "HybridPreBankControllerProbe")
target_compile_definitions(HybridControllerSetupProbe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_options(HybridControllerSetupProbe PRIVATE -static -static-libgcc -static-libstdc++)
add_dependencies(HybridControllerSetupProbe Mu2026Hybrid MockMuEngine NativeWorkerStub)
add_test(NAME HybridControllerSetupProbe COMMAND HybridControllerSetupProbe
    $<TARGET_FILE:Mu2026Hybrid> $<TARGET_FILE:MockMuEngine> $<TARGET_FILE:NativeWorkerStub>)

# Preview validation must retain assertions even in an optimized audio build.
add_test(NAME AnEngineTests COMMAND AnEngineTests)
get_property(previewTargets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(previewTarget IN LISTS previewTargets)
    if(previewTarget MATCHES "Tests$")
        target_compile_options(${previewTarget} PRIVATE -UNDEBUG)
    endif()
endforeach()
