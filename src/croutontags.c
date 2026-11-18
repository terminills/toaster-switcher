/* The names of the crouton tags (see croutontags.h). */
#include <ctype.h>
#include <stddef.h>
#include "croutontags.h"

/* A name's place is its number. Those that hold a table are marked with a '*'. */
static const char *const names[] = {
    NULL,
    "Version", "Revision", "AAeffect", "NonAAeffect", "KillInterfaceNonAA", "KillInterfaceAA", "ButtonELHlogic",
    "*CustomButtonELHlogic", "NumberOfAnims", "RequestFileName", "*AnimFiles", "Frames", "FieldSync",
    "VariableSpeeds", "ForcePlayForward", "ForcePlayReverse", "LoopAnims", "*AnimControl", "AudioFastSamples",
    "AudioMediumSamples", "AudioSlowSamples", "*AudioFiles", "*AudioControl", "*PauseTimes", "*LatchColors",
    "*TransparentColors", "*PaletteColors", "*Equations", "Encoder", "VerticalScroll", "ReverseTime",
    "ReverseButtLog", "*ReverseCustomButtLog", "KeyMode", "MatteColor", "CustomMatteColor", "BorderColor",
    "CustomBorderColor", "*LineNumbers", "LineNumberPlane", "ForceFreeze4", "ForceFreeze8", "ForceLive",
    "ForceLumKeyOn", "ForceLumKeyOff", "ForceLumKeyOnBlack", "ForceLumKeyOnWhite", "BadDefaultFX", "*LoadPictures",
    "FadeInDuration", "FadeOutDuration", "DigitalFX", "TimeMode", "LUT", "DigitalPairs", "LatchAM", "LatchBM",
    "LatchIS", "HonorPreviewOverLay", "ForceDefaultMatte", "TurnAudioFilterOff", "AudioStartField",
    "NumAudioFields", "ISandClipPause", "Interlaced", "FirstFieldNTSCII", "BounceILBM", "*LatchRanges",
    "*LatchList", "*TransparentRanges", "*TransparentList", "Color0Transparent", "AbortIfSlow",
    "NumSkipFieldsAtEnd", "TBarDoesAuto", "DoNotStompSprite", "AbortLoopAtEnd", "NonAAremap",
    "100PercentWhiteMatte", "UseEffectColor", "LoopCount", "CroutonType", "TimeLine", "IndexID", "FCountMode",
    "VariableFCount", "SlowFCount", "MedFCount", "FastFCount", "VariableFCount68000", "SlowFCount68000",
    "MedFCount68000", "FastFCount68000", "NumFields", "StartTime", "*DescriptorList", "*AboutList", "*CommentList",
    "AlgoFXtype", "AlgoFXborder", "NumFramesSlow", "NumFramesMedium", "NumFramesFast", "NumFramesVariable", "*Page",
    "Speed", "Delay", "Duration", "AudioAttack", "AudioDecay", "RecFields", "AudioOn", "AudioStart",
    "AudioDuration", "ClipStartField", "FadeInVideo", "MaxDuration", "VideoSource", "LoadedSlices",
    "*OriginalLocation", "AudioVolume1", "AudioVolume2", "AudioPan1", "AudioPan2", "PanelMode", "ColorMode",
    "CycleMode", "DataMode", "AdjustedVideoStart", "AdjustedVideoDuration", "Asynchronous", "*CommandLine",
    "*SMPTEtime", "TBarPosition", "HoldFields", "TakeOffset", "ASourceLen", "BSourceLen", "AudioFadeFlags",
    "AudEnv16", "AudEnv32", "AudEnv64", "AudEnv128",
};

int crouton_tag(const char *name, int *table)
{
    unsigned i;
    for (i = 1; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *a = names[i] + (names[i][0] == '*'), *b = name;
        while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) a++, b++;
        if (!*a && !*b) {
            *table = names[i][0] == '*';
            return i;
        }
    }
    return 0;
}
