#include "stdafx.h"
#include "xrCore/WebProfiler.h"

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"

#include <vorbis/vorbisfile.h>

CSoundRender_Source::~CSoundRender_Source() { unload(); }

namespace
{
bool ov_can_continue_read(long res)
{
    switch (res)
    {
    case 0:
        return false;
    // info
    case OV_HOLE:
        Msg("Vorbisfile encoutered missing or corrupt data in the bitstream. Recovery is normally automatic and this "
            "return code is for informational purposes only.");
        return true;
    case OV_EBADLINK:
        Msg("The given link exists in the Vorbis data stream, but is not decipherable due to garbage or corruption.");
        return true;
    // error
    case OV_FALSE: Msg("Not true, or no data available"); return false;
    case OV_EREAD: Msg("Read error while fetching compressed data for decode"); return false;
    case OV_EFAULT: Msg("Internal inconsistency in decode state. Continuing is likely not possible."); return false;
    case OV_EIMPL: Msg("Feature not implemented"); return false;
    case OV_EINVAL:
        Msg("Either an invalid argument, or incompletely initialized argument passed to libvorbisfile call");
        return false;
    case OV_ENOTVORBIS: Msg("The given file/data was not recognized as Ogg Vorbis data."); return false;
    case OV_EBADHEADER:
        Msg("The file/data is apparently an Ogg Vorbis stream, but contains a corrupted or undecipherable header.");
        return false;
    case OV_EVERSION: Msg("The bitstream format revision of the given stream is not supported."); return false;
    case OV_ENOSEEK: Msg("The given stream is not seekable"); return false;
    }
    return false;
}
}

void CSoundRender_Source::decompress(void* dest, u32 byte_offset, u32 size, OggVorbis_File* ovf) const
{
    XR_WEB_PROF(SoundDecode);
    ZoneScoped;

    // seek
    const auto sample_offset = ogg_int64_t(byte_offset / m_data_info.blockAlign);
    const u32 cur_pos = u32(ov_pcm_tell(ovf));
    if (cur_pos != sample_offset)
        ov_pcm_seek(ovf, sample_offset);

    // decompress
    if (m_data_info.format == SoundFormat::Float32)
        i_decompress(ovf, static_cast<float*>(dest), size);
    else
        i_decompress(ovf, static_cast<char*>(dest), size);
}

void CSoundRender_Source::i_decompress(OggVorbis_File* ovf, char* _dest, u32 size) const
{
    long TotalRet = 0;

    // Read loop
    while (TotalRet < static_cast<long>(size))
    {
        const auto ret = ov_read(ovf, _dest + TotalRet, size - TotalRet, 0, 2, 1, nullptr);
        if (ret <= 0 && !ov_can_continue_read(ret))
            break;
        TotalRet += ret;
    }
}

void CSoundRender_Source::i_decompress(OggVorbis_File* ovf, float* _dest, u32 size) const
{
    s32 left = s32(size / m_data_info.blockAlign);
    while (left)
    {
        float** pcm;
        long samples = ov_read_float(ovf, &pcm, left, nullptr);

        if (samples <= 0 && !ov_can_continue_read(samples))
            break;

        if (samples > left)
            samples = left;

        for (long j = 0; j < samples; j++)
            for (long i = 0; i < m_data_info.channels; i++)
                *_dest++ = clampr(pcm[i][j], -1.0f, 1.0f);

        left -= samples;
    }
}

constexpr ov_callbacks g_ov_callbacks =
{
    // read
    [](void* ptr, size_t size, size_t nmemb, void* datasource) -> size_t
    {
        auto* file = static_cast<IReader*>(datasource);
        const size_t exist_block = _max(0ul, iFloor(file->elapsed() / (float)size));
        const size_t read_block = std::min(exist_block, nmemb);
        file->r(ptr, read_block * size);
        return read_block;
    },
    // seek
    [](void* datasource, ogg_int64_t offset, int whence) -> int
    {
        //SEEK_SET 0 File beginning
        //SEEK_CUR 1 Current file pointer position
        //SEEK_END 2 End-of-file
        switch (whence)
        {
        case SEEK_SET: ((IReader*)datasource)->seek((int)offset); break;
        case SEEK_CUR: ((IReader*)datasource)->advance((int)offset); break;
        case SEEK_END: ((IReader*)datasource)->seek((int)offset + ((IReader*)datasource)->length()); break;
        }
        return 0;
    },
    // close
    [](void* datasource) -> int
    {
        auto* file = static_cast<IReader*>(datasource);
        FS.r_close(file);
        return 0;
    },
    // tell
    [](void* datasource) -> long
    {
        const auto file = static_cast<IReader*>(datasource);
        return static_cast<long>(file->tell());
    },
};

OggVorbis_File* CSoundRender_Source::open() const
{
    const auto file = FS.r_open(pname.c_str());
    R_ASSERT3(file && file->length(), "Can't open wave file:", pname.c_str());

    const auto ovf = xr_new<OggVorbis_File>();
    ov_open_callbacks(file, ovf, nullptr, 0, g_ov_callbacks);

    return ovf;
}

void CSoundRender_Source::close(OggVorbis_File*& ovf) const
{
    ov_clear(ovf);
    xr_delete(ovf);
}

void CSoundRender_Source::SetupDataInfo(u32 samplesPerSec, u16 channels, s64 pcmTotal)
{
    m_data_info = {};

    m_data_info.samplesPerSec = samplesPerSec;
    m_data_info.channels = channels;

    if (SoundRender->supports_float_pcm)
    {
        m_data_info.format = SoundFormat::Float32;
        m_data_info.bitsPerSample = 32;
    }
    else
    {
        m_data_info.format = SoundFormat::PCM;
        m_data_info.bitsPerSample = 16;
    }

    m_data_info.blockAlign = m_data_info.bitsPerSample / 8 * m_data_info.channels;
    m_data_info.avgBytesPerSec = m_data_info.samplesPerSec * m_data_info.blockAlign;
    m_data_info.bytesPerBuffer = sdef_target_block * m_data_info.avgBytesPerSec / 1000;

    dwBytesTotal = u32(pcmTotal * m_data_info.blockAlign);
    fTimeTotal = dwBytesTotal / float(m_data_info.avgBytesPerSec);
}

/*
    Sound index: everything LoadWave learns from an ogg's headers, for every
    file under $game_sounds$. Creating a source only needs that data, and the
    browser build has no lazy mmap: LoadWave there reads each whole file, and
    an NPC's spawn creates its full voice set (hundreds of files, ~0.5s of a
    frozen frame per group of NPCs). Generated by a native run with
    -dump_sound_index into $app_data_root$/sound_index.bin; entries whose file
    size no longer matches are ignored, so a stale index only costs speed.

    [u32] 'XRSI' [u32] version [u32] count, then per entry:
    [u16] id length, id (lowercase, '\\' separators, no extension),
    [u32] file size, [u32] rate, [u16] channels, [s64] pcm total,
    [f32] base volume, min dist, max dist, max AI dist, [u32] game type
*/
namespace
{
constexpr u32 SOUND_INDEX_MAGIC = 0x49535258; // 'XRSI'
constexpr u32 SOUND_INDEX_VERSION = 1;

struct SoundIndexEntry
{
    u32 fileSize;
    u32 rate;
    u16 channels;
    s64 pcmTotal;
    SoundSourceInfo info;
};

xr_string sound_index_key(pcstr name)
{
    xr_string key = name;
    for (char& c : key)
        c = c == '/' ? '\\' : char(tolower(u8(c)));
    if (const auto dot = key.rfind('.'); dot != xr_string::npos && key.find('\\', dot) == xr_string::npos)
        key.erase(dot);
    return key;
}

#ifdef XR_PLATFORM_WEB
const xr_unordered_map<xr_string, SoundIndexEntry>& sound_index()
{
    static xr_unordered_map<xr_string, SoundIndexEntry> index;
    static bool loaded = false;
    if (loaded)
        return index;
    loaded = true;

    IReader* F = FS.r_open("$app_data_root$", "sound_index.bin");
    if (!F)
    {
        Msg("~ SOUND: no sound index, sources are read in full when created");
        return index;
    }
    if (F->r_u32() == SOUND_INDEX_MAGIC && F->r_u32() == SOUND_INDEX_VERSION)
    {
        const u32 count = F->r_u32();
        index.reserve(count);
        for (u32 i = 0; i < count; ++i)
        {
            const u16 len = F->r_u16();
            xr_string id(static_cast<pcstr>(F->pointer()), len);
            F->advance(len);
            SoundIndexEntry e;
            e.fileSize = F->r_u32();
            e.rate = F->r_u32();
            e.channels = F->r_u16();
            F->r(&e.pcmTotal, sizeof(e.pcmTotal));
            e.info.baseVolume = F->r_float();
            e.info.minDist = F->r_float();
            e.info.maxDist = F->r_float();
            e.info.maxAIDist = F->r_float();
            e.info.gameType = F->r_u32();
            index.emplace(std::move(id), e);
        }
        Msg("* SOUND: sound index with %u sources", count);
    }
    FS.r_close(F);
    return index;
}
#endif
} // namespace

bool CSoundRender_Source::LoadFromIndex([[maybe_unused]] pcstr id, [[maybe_unused]] pcstr path)
{
#ifdef XR_PLATFORM_WEB
    const auto& index = sound_index();
    const auto it = index.find(sound_index_key(id));
    if (it == index.end())
        return false;
    const auto* desc = FS.GetFileDesc(path);
    if (!desc || desc->size_real != it->second.fileSize)
        return false; // changed since the index was made
    pname = path;
    SetupDataInfo(it->second.rate, it->second.channels, it->second.pcmTotal);
    m_info = it->second.info;
    return true;
#else
    return false;
#endif
}

void CSoundRender_Source::DumpIndex(pcstr path)
{
    xr_vector<pstr>* files = FS.file_list_open("$game_sounds$", FS_ListFiles);
    if (!files)
        return;
    IWriter* W = FS.w_open(path);
    if (!W)
    {
        FS.file_list_close(files);
        return;
    }
    W->w_u32(SOUND_INDEX_MAGIC);
    W->w_u32(SOUND_INDEX_VERSION);
    const size_t countPos = W->tell();
    W->w_u32(0);
    u32 count = 0;
    for (pcstr name : *files)
    {
        const size_t nameLen = xr_strlen(name);
        if (nameLen < 4 || xr_stricmp(name + nameLen - 4, ".ogg") != 0)
            continue;
        string_path fn;
        FS.update_path(fn, "$game_sounds$", name);
        const auto* desc = FS.GetFileDesc(fn);
        if (!desc)
            continue;

        CSoundRender_Source source;
        if (!source.LoadWave(fn)) // same parsing the engine would do
            continue;

        const xr_string key = sound_index_key(name);
        W->w_u16(u16(key.size()));
        W->w(key.data(), key.size());
        W->w_u32(desc->size_real);
        W->w_u32(source.m_data_info.samplesPerSec);
        W->w_u16(source.m_data_info.channels);
        const s64 pcmTotal = s64(source.dwBytesTotal / source.m_data_info.blockAlign);
        W->w(&pcmTotal, sizeof(pcmTotal));
        W->w_float(source.m_info.baseVolume);
        W->w_float(source.m_info.minDist);
        W->w_float(source.m_info.maxDist);
        W->w_float(source.m_info.maxAIDist);
        W->w_u32(source.m_info.gameType);
        ++count;
    }
    W->seek(countPos);
    W->w_u32(count);
    FS.w_close(W);
    FS.file_list_close(files);
    Msg("* SOUND: wrote sound index (%u sources) to %s", count, path);
}

bool CSoundRender_Source::LoadWave(pcstr pName)
{
    ZoneScoped;

    pname = pName;

    // Load file into memory and parse WAV-format
    OggVorbis_File ovf;
    {
        IReader* wave = FS.r_open(pname.c_str());
        R_ASSERT3(wave && wave->length(), "Can't open wave file:", pname.c_str());
        ov_open_callbacks(wave, &ovf, nullptr, 0, g_ov_callbacks);
    }

    const vorbis_info* ovi = ov_info(&ovf, -1);

    // verify
    R_ASSERT3_CURE(ovi, "Invalid source info:", pName,
    {
        ov_clear(&ovf);
        return false;
    });

    SetupDataInfo(ovi->rate, u16(ovi->channels), ov_pcm_total(&ovf, -1));

    m_info = {};

    const vorbis_comment* ovm = ov_comment(&ovf, -1);
    if (ovm->comments)
    {
        IReader F(ovm->user_comments[0], ovm->comment_lengths[0]);
        const u32 vers = F.r_u32();
        if (vers == 0x0001)
        {
            m_info.minDist = F.r_float();
            m_info.maxDist = F.r_float();
            m_info.baseVolume = 1.f;
            m_info.gameType = F.r_u32();
            m_info.maxAIDist = m_info.maxDist;
        }
        else if (vers == 0x0002)
        {
            m_info.minDist = F.r_float();
            m_info.maxDist = F.r_float();
            m_info.baseVolume = F.r_float();
            m_info.gameType = F.r_u32();
            m_info.maxAIDist = m_info.maxDist;
        }
        else if (vers == OGG_COMMENT_VERSION)
        {
            m_info.minDist = F.r_float();
            m_info.maxDist = F.r_float();
            m_info.baseVolume = F.r_float();
            m_info.gameType = F.r_u32();
            m_info.maxAIDist = F.r_float();
        }
        else
        {
#ifndef MASTER_GOLD
            Log("! Invalid ogg-comment version, file: ", pName);
#endif
        }
    }
    else
    {
#ifndef MASTER_GOLD
        Log("! Missing ogg-comment, file: ", pName);
#endif
    }

    R_ASSERT3_CURE(m_info.maxAIDist >= 0.1f && m_info.maxDist >= 0.1f, "Invalid max distance.", pName,
    {
        ov_clear(&ovf);
        return false;
    });

    ov_clear(&ovf);
    return true;
}

bool CSoundRender_Source::load(pcstr name)
{
    XR_WEB_PROF(SoundLoad);
    string_path fn, N;
    xr_strcpy(N, name);
#ifdef XR_PLATFORM_WINDOWS
    xr_strlwr(N);
#endif

    if (strext(N))
        *strext(N) = 0;

    fname = N;

    strconcat(fn, N, ".ogg");
    if (!FS.exist("$level$", fn))
        FS.update_path(fn, "$game_sounds$", fn);

#ifndef MASTER_GOLD
    if (!FS.exist(fn))
    {
        Msg("~ %s: Can't find sound '%s'", __FUNCTION__, name);
#   ifdef _EDITOR
        FS.update_path(fn, "$game_sounds$", "$no_sound.ogg");
#   endif
    }
#endif

    if (FS.exist(fn))
    {
        if (LoadFromIndex(N, fn))
            return true;
        if (LoadWave(fn))
            return true;
    }

    return false;
}

void CSoundRender_Source::unload()
{
    fTimeTotal = 0.0f;
    dwBytesTotal = 0;
}
