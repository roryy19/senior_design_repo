import { NativeModules, Platform } from 'react-native';

const { TtsSynthesizer } = NativeModules;

// Debug: log whether the native module was found at startup
console.log('[TTS] Native module available:', !!TtsSynthesizer);
console.log('[TTS] Platform:', Platform.OS);
console.log('[TTS] All native modules:', Object.keys(NativeModules).filter(k => k.toLowerCase().includes('tts')));

/**
 * Generate TTS audio for the given text using the iOS native module.
 *
 * Returns base64-encoded raw PCM audio:
 *   - 8-bit unsigned (0-255, silence = 128)
 *   - 8kHz sample rate
 *   - Mono
 *   - ~8KB per second of speech
 *
 * Only available on iOS with a dev build (not Expo Go).
 * Returns null on Android or if the native module is unavailable.
 */
export async function generateSpeechAudio(text: string): Promise<string | null> {
  if (Platform.OS !== 'ios' || !TtsSynthesizer) {
    console.warn('TtsSynthesizer native module not available');
    return null;
  }

  try {
    const base64Pcm: string = await TtsSynthesizer.generate(text);
    return base64Pcm;
  } catch (e) {
    console.warn('TTS generation failed:', e);
    return null;
  }
}
