using System;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using LibVLCSharp;
using UnityEngine;

/// <summary>
/// Basic implementation for outputting VLC audio through a Unity Audio Source. 
/// With this implementation, you will gain ability to have 3D audio, AudioSource effects, and anything else that 
/// AudioSources support.
/// </summary>
[RequireComponent(typeof(AudioSource))]
public class VLCAudioSource : MonoBehaviour
{
    // User desired Sample Rate and Channels
    public int SampleRate = 48000;
    public int Channels = 2;
    
    // The audio source attached to the GameObject
    private AudioSource audioSource;
    // The audio clip that will receive the converted VLC audio
    private AudioClip audioClip;
    // The buffer size for Unity audio (power of 2 for fast modulo)
    private int bufferSize;
    // Mask for fast modulo operations (bufferSize - 1)
    private int bufferMask;
    // The unity audio buffer
    private float[] buffer;
    // Pinned buffer handle to avoid repeated pinning
    private GCHandle bufferHandle;
    // Pinned buffer pointer
    private IntPtr bufferPtr;
    // The writing position for the buffer (atomic access)
    private int writePosition;
    // The reading position for the buffer (atomic access)
    private int readPosition;
    // Flag to signal a pending flush from VLC thread
    private int flushFlag;

    public void Attach(MediaPlayer mediaPlayer)
    {
        // Get the attached AudioSource (MUST be on this GameObject)
        audioSource = GetComponent<AudioSource>();
        // Loop the audio source
        audioSource.loop = true;
        // Tell the media player to be in Unity's audio format and convert the media to the user specified
        mediaPlayer.SetAudioFormat("FL32", (uint) SampleRate, (uint) Channels);
        // Audio Callbacks for VLC to our functions
        mediaPlayer.SetAudioCallbacks(OnAudioCallback, null, null, OnFlush, null);
        // Create the buffer array (use power of 2 for fast modulo operations)
        int desiredSize = SampleRate * Channels;
        bufferSize = Mathf.NextPowerOfTwo(desiredSize);
        bufferMask = bufferSize - 1;
        buffer = new float[bufferSize];
        bufferHandle = GCHandle.Alloc(buffer, GCHandleType.Pinned);
        bufferPtr = bufferHandle.AddrOfPinnedObject();
        // Create the audio clip and initialize the AudioSource
        audioClip = AudioClip.Create("VLCAudio", bufferSize, Channels, SampleRate, true, OnAudioRead);
        audioSource.clip = audioClip;
        audioSource.Play();
    }

    private void OnDestroy()
    {
        if (bufferHandle.IsAllocated)
        {
            bufferHandle.Free();
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private unsafe void OnAudioCallback(IntPtr data, IntPtr samplesPtr, uint count, long pts)
    {
        // Handle pending flush (VLC serializes flush and play callbacks,
        // so resetting both positions here is safe before any new write)
        if (Volatile.Read(ref flushFlag) == 1)
        {
            writePosition = 0;
            readPosition = 0;
            Volatile.Write(ref flushFlag, 0);
        }

        int floatCount = (int) count * Channels;

        int wp = Volatile.Read(ref writePosition);
        int rp = Volatile.Read(ref readPosition);
        int spaceToEnd = buffer.Length - wp;

        float* src = (float*)samplesPtr;
        float* dst = (float*)bufferPtr;

        if (floatCount <= spaceToEnd)
        {
            // No wrap-around - single direct memory copy
            Buffer.MemoryCopy(src, dst + wp, (buffer.Length - wp) * sizeof(float), floatCount * sizeof(float));
            int newWp = (wp + floatCount) & bufferMask;
            Volatile.Write(ref writePosition, newWp);
        }
        else
        {
            // Wrap-around - two memory copies
            Buffer.MemoryCopy(src, dst + wp, spaceToEnd * sizeof(float), spaceToEnd * sizeof(float));
            int remaining = floatCount - spaceToEnd;
            Buffer.MemoryCopy(src + spaceToEnd, dst, remaining * sizeof(float), remaining * sizeof(float));
            int newWp = remaining & bufferMask;
            Volatile.Write(ref writePosition, newWp);
        }

        // Handle buffer overflow - if we overwrote unread data, advance read position
        // This is safe because we're the only writer
        int newWp2 = Volatile.Read(ref writePosition);
        int availableSpace = (rp - newWp2 - 1) & bufferMask;
        if (floatCount > availableSpace)
        {
            Volatile.Write(ref readPosition, (newWp2 + 1) & bufferMask);
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private unsafe void OnAudioRead(float[] data)
    {
        // If a flush is pending, output silence until the writer resets positions
        if (Volatile.Read(ref flushFlag) == 1)
        {
            for (int i = 0; i < data.Length; i++)
                data[i] = 0f;
            return;
        }

        int rp = Volatile.Read(ref readPosition);
        int wp = Volatile.Read(ref writePosition);
        int available = (wp - rp) & bufferMask;
        int toRead = available < data.Length ? available : data.Length;

        float* src = (float*)bufferPtr;

        fixed (float* dst = data)
        {
            if (toRead > 0)
            {
                int spaceToEnd = buffer.Length - rp;

                if (toRead <= spaceToEnd)
                {
                    // No wrap-around - single memory copy
                    Buffer.MemoryCopy(src + rp, dst, data.Length * sizeof(float), toRead * sizeof(float));
                    Volatile.Write(ref readPosition, (rp + toRead) & bufferMask);
                }
                else
                {
                    // Wrap-around - two memory copies
                    Buffer.MemoryCopy(src + rp, dst, data.Length * sizeof(float), spaceToEnd * sizeof(float));
                    int remaining = toRead - spaceToEnd;
                    Buffer.MemoryCopy(src, dst + spaceToEnd, (data.Length - spaceToEnd) * sizeof(float), remaining * sizeof(float));
                    Volatile.Write(ref readPosition, remaining & bufferMask);
                }
            }

            // Fill remaining with silence if underrun using SIMD
            if (toRead < data.Length)
            {
                int silenceCount = data.Length - toRead;
                float* silencePtr = dst + toRead;

                int simdLength = Vector<float>.Count;
                int vectorCount = silenceCount / simdLength;

                for (int i = 0; i < vectorCount; i++)
                {
                    *(Vector<float>*)(silencePtr + i * simdLength) = Vector<float>.Zero;
                }

                // Handle remaining elements
                int remaining = silenceCount - (vectorCount * simdLength);
                for (int i = 0; i < remaining; i++)
                {
                    silencePtr[vectorCount * simdLength + i] = 0f;
                }
            }
        }
    }
    
    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    private void OnFlush(IntPtr data, long l)
    {
        Volatile.Write(ref flushFlag, 1);
    }
}
