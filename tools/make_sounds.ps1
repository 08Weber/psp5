# psp5 - made by 08Weber
# Decodes selected PS5_Sound/*.flac files via Windows Media Foundation into 44.1 kHz
# 16-bit mono PCM, trims silence, balances gain without clipping, and compresses them
# into RFC-1950 zlib streams in src/ps5_sounds.h (decompressed with <zlib.h> at boot).
# Run in Windows: powershell -ExecutionPolicy Bypass -File tools\make_sounds.ps1
param([string]$SoundDir = '')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
if (-not $SoundDir) { $SoundDir = Join-Path $project 'PS5_Sound' }

$cs = @'
using System;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;

public static class Ps5SoundTool {
    [DllImport("mfplat.dll", ExactSpelling = true)]
    static extern int MFStartup(int version, int flags);
    [DllImport("mfplat.dll", ExactSpelling = true)]
    static extern int MFShutdown();
    [DllImport("mfplat.dll", ExactSpelling = true)]
    static extern int MFCreateMediaType(out IMFMediaType ppMFType);
    [DllImport("mfreadwrite.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
    static extern int MFCreateSourceReaderFromURL(string pwszURL, IntPtr pAttributes, out IMFSourceReader ppSourceReader);

    static readonly Guid MFMediaType_Audio = new Guid("73647561-0000-0010-8000-00AA00389B71");
    static readonly Guid MFAudioFormat_PCM = new Guid("00000001-0000-0010-8000-00AA00389B71");
    static readonly Guid MF_MT_MAJOR_TYPE = new Guid("48eba18e-f8c9-4687-bf11-0a74c9f96a8f");
    static readonly Guid MF_MT_SUBTYPE = new Guid("f7e34c9a-42e8-4714-b74b-cb29d72c35e5");
    static readonly Guid MF_MT_AUDIO_NUM_CHANNELS = new Guid("37e48bf5-645e-4c5b-89de-ada9e29b696a");
    static readonly Guid MF_MT_AUDIO_SAMPLES_PER_SECOND = new Guid("5faeeae7-0290-4c31-9e8a-c534f68d9dba");
    static readonly Guid MF_MT_AUDIO_BITS_PER_SAMPLE = new Guid("f2deb57f-40fa-4764-aa33-ed4f2d1ff669");

    const uint MF_SOURCE_READER_FIRST_AUDIO_STREAM = 0xFFFFFFFD;
    const uint MF_SOURCE_READERF_ENDOFSTREAM = 2;

    [ComImport, Guid("2cd2d921-c447-44a7-a13c-4adabfc247e3"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMFAttributes {
        void _a01(); void _a02(); void _a03(); void _a04(); void _a05(); void _a06(); void _a07(); void _a08(); void _a09(); void _a10();
        void _a11(); void _a12(); void _a13(); void _a14(); void _a15(); void _a16(); void _a17(); void _a18();
        [PreserveSig] int SetUINT32(ref Guid guidKey, uint unValue);
        void _a20(); void _a21();
        [PreserveSig] int SetGUID(ref Guid guidKey, ref Guid guidValue);
    }
    [ComImport, Guid("44ae0fa8-ea31-4109-8d2e-4cae4997c555"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMFMediaType : IMFAttributes {}
    [ComImport, Guid("045fa593-8799-42b8-bc8d-8968c6453507"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMFMediaBuffer {
        [PreserveSig] int Lock(out IntPtr ppbBuffer, out uint pcbMaxLength, out uint pcbCurrentLength);
        [PreserveSig] int Unlock();
    }
    [ComImport, Guid("c40a00f2-b93a-4d80-ae8c-5a1c634f58e4"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMFSample {
        void _s01(); void _s02(); void _s03(); void _s04(); void _s05(); void _s06(); void _s07(); void _s08(); void _s09(); void _s10();
        void _s11(); void _s12(); void _s13(); void _s14(); void _s15(); void _s16(); void _s17(); void _s18(); void _s19(); void _s20();
        void _s21(); void _s22(); void _s23(); void _s24(); void _s25(); void _s26(); void _s27(); void _s28(); void _s29(); void _s30();
        void _s31(); void _s32(); void _s33(); void _s34(); void _s35(); void _s36(); void _s37(); void _s38();
        [PreserveSig] int ConvertToContiguousBuffer(out IMFMediaBuffer ppBuffer);
    }
    [ComImport, Guid("70ae66f2-c809-4e4f-8915-bdcb406b7993"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMFSourceReader {
        [PreserveSig] int GetStreamSelection(uint dwStreamIndex, out bool pfSelected);
        [PreserveSig] int SetStreamSelection(uint dwStreamIndex, bool fSelected);
        [PreserveSig] int GetNativeMediaType(uint dwStreamIndex, uint dwMediaTypeIndex, out IMFMediaType ppMediaType);
        [PreserveSig] int GetCurrentMediaType(uint dwStreamIndex, out IMFMediaType ppMediaType);
        [PreserveSig] int SetCurrentMediaType(uint dwStreamIndex, IntPtr pdwReserved, IMFMediaType pMediaType);
        void _SetCurrentPosition();
        [PreserveSig] int ReadSample(uint dwStreamIndex, uint dwControlFlags, out uint pdwActualStreamIndex, out uint pdwStreamFlags, out long pllTimestamp, out IMFSample ppSample);
    }

    public static short[] DecodeMono44100(string path) {
        int hr = MFStartup(0x00020070, 1);
        if (hr < 0) throw new Exception("MFStartup " + hr.ToString("X"));
        try {
            IMFSourceReader reader;
            hr = MFCreateSourceReaderFromURL(path, IntPtr.Zero, out reader);
            if (hr < 0) throw new Exception("Reader " + hr.ToString("X"));
            IMFMediaType mt;
            MFCreateMediaType(out mt);
            Guid g1 = MF_MT_MAJOR_TYPE, v1 = MFMediaType_Audio; mt.SetGUID(ref g1, ref v1);
            Guid g2 = MF_MT_SUBTYPE, v2 = MFAudioFormat_PCM; mt.SetGUID(ref g2, ref v2);
            Guid g3 = MF_MT_AUDIO_NUM_CHANNELS; mt.SetUINT32(ref g3, 1);
            Guid g4 = MF_MT_AUDIO_SAMPLES_PER_SECOND; mt.SetUINT32(ref g4, 44100);
            Guid g5 = MF_MT_AUDIO_BITS_PER_SAMPLE; mt.SetUINT32(ref g5, 16);
            hr = reader.SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, IntPtr.Zero, mt);
            if (hr < 0) throw new Exception("SetMT " + hr.ToString("X"));
            using (var ms = new MemoryStream()) {
                while (true) {
                    uint idx, flags; long ts; IMFSample samp;
                    hr = reader.ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, out idx, out flags, out ts, out samp);
                    if (hr < 0) throw new Exception("ReadSample " + hr.ToString("X"));
                    if (samp != null) {
                        IMFMediaBuffer buf;
                        samp.ConvertToContiguousBuffer(out buf);
                        IntPtr ptr; uint maxLen, curLen;
                        buf.Lock(out ptr, out maxLen, out curLen);
                        byte[] tmp = new byte[curLen];
                        Marshal.Copy(ptr, tmp, 0, (int)curLen);
                        ms.Write(tmp, 0, (int)curLen);
                        buf.Unlock();
                        Marshal.ReleaseComObject(buf);
                        Marshal.ReleaseComObject(samp);
                    }
                    if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
                }
                Marshal.ReleaseComObject(mt);
                Marshal.ReleaseComObject(reader);
                byte[] raw = ms.ToArray();
                short[] pcm = new short[raw.Length / 2];
                Buffer.BlockCopy(raw, 0, pcm, 0, pcm.Length * 2);
                return pcm;
            }
        } finally {
            MFShutdown();
        }
    }

    public static byte[] ProcessAndCompress(short[] src, double targetRms, double maxPeak, double maxSec, bool isBoot, out int outSamples) {
        int rate = 44100;
        int n = src.Length;
        int maxSamples = (int)(maxSec * rate);
        int peak = 0;
        for (int i = 0; i < n; i++) {
            int a = Math.Abs((int)src[i]);
            if (a > peak) peak = a;
        }
        int thresh = Math.Max(20, (int)(peak * 0.008));
        int lead = 0, trail = n - 1;
        if (!isBoot) {
            lead = n;
            for (int i = 0; i < n; i++) {
                if (Math.Abs((int)src[i]) >= thresh) { lead = i; break; }
            }
            if (lead >= n) lead = 0;
            lead = Math.Max(0, lead - 44); // ~1 ms pre-roll
            trail = lead;
            for (int i = n - 1; i >= lead; i--) {
                if (Math.Abs((int)src[i]) >= thresh) { trail = i; break; }
            }
            trail = Math.Min(n - 1, trail + 900); // ~20 ms tail
        }
        int count = Math.Min(maxSamples, trail - lead + 1);
        if (count < 1) count = 1;
        outSamples = count;

        int winPeak = 1;
        double sumSq = 0;
        int actN = 0;
        for (int i = 0; i < count; i++) {
            int a = Math.Abs((int)src[lead + i]);
            if (a > winPeak) winPeak = a;
            if (a >= thresh * 2) {
                double norm = a / 32767.0;
                sumSq += norm * norm;
                actN++;
            }
        }
        double activeRms = actN > 0 ? Math.Sqrt(sumSq / actN) : (winPeak / 32767.0);
        double gain = targetRms / Math.Max(activeRms, 1e-4);
        double peakCap = (maxPeak * 32767.0) / winPeak;
        // Soft ceiling allows RMS-targeted chimes to stay loud without clipping transients
        if (!isBoot && gain > peakCap * 1.35) gain = peakCap * 1.35;
        if (isBoot && gain > peakCap) gain = peakCap;
        if (gain > 28.0) gain = 28.0;

        int fadeIn = Math.Min(count / 4, isBoot ? 220 : 44);
        int fadeOut = Math.Min(count / 2, isBoot ? (int)(0.90 * rate) : 660);
        double knee = 0.78, head = 0.94 - knee;
        byte[] rawBytes = new byte[count * 2];
        for (int i = 0; i < count; i++) {
            double env = 1.0;
            if (i < fadeIn) env *= 0.5 - 0.5 * Math.Cos(Math.PI * i / fadeIn);
            if (i >= count - fadeOut) {
                double rem = (double)(count - 1 - i) / fadeOut;
                env *= isBoot ? (rem * rem) : (0.5 - 0.5 * Math.Cos(Math.PI * rem));
            }
            double x = (src[lead + i] / 32767.0) * gain * env;
            double ax = Math.Abs(x);
            if (ax > knee) {
                double u = (ax - knee) / head;
                ax = knee + head * Math.Tanh(u);
                x = x < 0 ? -ax : ax;
            }
            int iv = (int)Math.Round(x * 32767.0);
            if (iv > 32767) iv = 32767;
            if (iv < -32768) iv = -32768;
            rawBytes[i * 2] = (byte)(iv & 0xFF);
            rawBytes[i * 2 + 1] = (byte)((iv >> 8) & 0xFF);
        }

        // Wrap DeflateStream in RFC-1950 zlib stream (0x78 0xDA + deflate + big-endian Adler32)
        uint s1 = 1, s2 = 0;
        for (int i = 0; i < rawBytes.Length; i++) {
            s1 = (s1 + rawBytes[i]) % 65521;
            s2 = (s2 + s1) % 65521;
        }
        uint adler = (s2 << 16) | s1;
        using (var ms = new MemoryStream()) {
            ms.WriteByte(0x78);
            ms.WriteByte(0xDA);
            using (var ds = new DeflateStream(ms, CompressionLevel.Optimal, true)) {
                ds.Write(rawBytes, 0, rawBytes.Length);
            }
            ms.WriteByte((byte)(adler >> 24));
            ms.WriteByte((byte)((adler >> 16) & 0xFF));
            ms.WriteByte((byte)((adler >> 8) & 0xFF));
            ms.WriteByte((byte)(adler & 0xFF));
            return ms.ToArray();
        }
    }
}
'@
Add-Type -TypeDefinition $cs

# Order MUST match SoundId enum in src/sound.h
$specs = @(
    @{ Name = 'move';       File = '17. Focus Move Psfx Focus Move.flac';              Rms = 0.20; Peak = 0.72; Max = 0.45; Boot = $false },
    @{ Name = 'swipe';      File = '08. Change Space Psfx Change Spac.flac';            Rms = 0.21; Peak = 0.76; Max = 0.50; Boot = $false },
    @{ Name = 'panel';      File = '07. Change Panel2 Psfx Change Pane.flac';           Rms = 0.20; Peak = 0.72; Max = 0.45; Boot = $false },
    @{ Name = 'select';     File = '11. Enter Psfx Enter.flac';                         Rms = 0.23; Peak = 0.84; Max = 1.10; Boot = $false },
    @{ Name = 'back';       File = '05. Cancel Psfx Cancel.flac';                       Rms = 0.22; Peak = 0.80; Max = 1.15; Boot = $false },
    @{ Name = 'toggle_on';  File = '36. Switch on Psfx Switch On.flac';                 Rms = 0.22; Peak = 0.80; Max = 0.45; Boot = $false },
    @{ Name = 'toggle_off'; File = '35. Switch Off Psfx Switch Off.flac';               Rms = 0.22; Peak = 0.80; Max = 0.45; Boot = $false },
    @{ Name = 'slider';     File = '34. Slider Level Meter Psfx Slider Leve.flac';       Rms = 0.21; Peak = 0.74; Max = 0.35; Boot = $false },
    @{ Name = 'open';       File = '29. Open Option Menu Psfx Open Option.flac';        Rms = 0.22; Peak = 0.82; Max = 0.90; Boot = $false },
    @{ Name = 'close_menu'; File = '10. Close Option Menu Psfx Close Optio.flac';       Rms = 0.21; Peak = 0.78; Max = 0.90; Boot = $false },
    @{ Name = 'cc_open';    File = '25. Open Control Center Psfx Open Contro.flac';     Rms = 0.23; Peak = 0.84; Max = 0.95; Boot = $false },
    @{ Name = 'cc_close';   File = '09. Close Control Center Psfx Close Contr.flac';    Rms = 0.22; Peak = 0.80; Max = 0.95; Boot = $false },
    @{ Name = 'launch';     File = '28. Open Home Psfx Open Home.flac';                 Rms = 0.25; Peak = 0.88; Max = 2.00; Boot = $false },
    @{ Name = 'error';      File = '13. Error Psfx Error.flac';                         Rms = 0.22; Peak = 0.82; Max = 0.85; Boot = $false },
    @{ Name = 'screenshot'; File = '37. Take Screenshot Psfx Take Screen.flac';         Rms = 0.23; Peak = 0.84; Max = 0.40; Boot = $false },
    @{ Name = 'key_move';   File = '16. Focus Move in Keyboard Psfx Focus Move.flac';   Rms = 0.19; Peak = 0.68; Max = 0.20; Boot = $false },
    @{ Name = 'key_type';   File = '38. Text Input Psfx Text Input.flac';               Rms = 0.21; Peak = 0.76; Max = 0.20; Boot = $false },
    @{ Name = 'key_back';   File = '04. Backspace Psfx Backspace.flac';                 Rms = 0.21; Peak = 0.76; Max = 0.20; Boot = $false },
    @{ Name = 'key_error';  File = '12. Error in Text Box Psfx Error in Te.flac';       Rms = 0.21; Peak = 0.78; Max = 0.20; Boot = $false },
    @{ Name = 'confirm';    File = '45. Yes in Dialog Psfx Yes in Dial.flac';           Rms = 0.23; Peak = 0.84; Max = 1.00; Boot = $false },
    @{ Name = 'boot';       File = '001. First Time Startup.flac';                      Rms = 0.26; Peak = 0.88; Max = 5.75; Boot = $true  }
)

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('/* psp5 - made by 08Weber */')
[void]$sb.AppendLine('/* Generated by tools/make_sounds.ps1 - do not edit by hand. */')
[void]$sb.AppendLine('#define PS5_SND_RATE 44100')
[void]$sb.AppendLine('')

$entries = New-Object System.Collections.Generic.List[string]
$totRaw = 0; $totZ = 0
foreach ($s in $specs) {
    $path = Join-Path $SoundDir $s.File
    $pcm = [Ps5SoundTool]::DecodeMono44100($path)
    $samples = 0
    $z = [Ps5SoundTool]::ProcessAndCompress($pcm, $s.Rms, $s.Peak, $s.Max, $s.Boot, [ref]$samples)
    $rawBytes = $samples * 2
    if (-not $s.Boot) { $totRaw += $rawBytes }
    $totZ += $z.Length
    "{0,-12} {1,5}ms  raw={2,6}B  zlib={3,6}B" -f $s.Name, [int]($samples * 1000 / 44100), $rawBytes, $z.Length
    [void]$sb.AppendLine("static const unsigned char ps5_snd_$($s.Name)_z[] = {")
    for ($i = 0; $i -lt $z.Length; $i += 16) {
        $end = [Math]::Min($z.Length - 1, $i + 15)
        $line = ($z[$i..$end] | ForEach-Object { $_.ToString() }) -join ','
        [void]$sb.AppendLine("    $line,")
    }
    [void]$sb.AppendLine('};')
    $entries.Add("    { ps5_snd_$($s.Name)_z, sizeof(ps5_snd_$($s.Name)_z), $samples },")
}
[void]$sb.AppendLine('')
[void]$sb.AppendLine('typedef struct { const unsigned char *z; int zlen; int samples; } Ps5SoundAsset;')
[void]$sb.AppendLine("static const Ps5SoundAsset PS5_ASSETS[$($specs.Count)] = {")
foreach ($e in $entries) { [void]$sb.AppendLine($e) }
[void]$sb.AppendLine('};')

$outPath = Join-Path $project 'src\ps5_sounds.h'
[IO.File]::WriteAllText($outPath, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
"Persistent RAM SFX: $([int]($totRaw/1024)) KB | Total zlib in EBOOT: $([int]($totZ/1024)) KB -> $outPath"
