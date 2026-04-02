/**
 * Native iOS module that generates TTS audio as raw PCM data.
 *
 * Uses AVSpeechSynthesizer writeUtterance:toBufferCallback: to render
 * speech to an audio buffer instead of playing it through the speaker.
 * The audio is converted to 8-bit unsigned PCM at 8kHz mono — the
 * format the ESP32 belt expects.
 *
 * Exposed to React Native as: NativeModules.TtsSynthesizer.generate(text)
 * Returns a base64-encoded string of raw PCM bytes.
 */

#import <React/RCTBridgeModule.h>
#import <AVFoundation/AVFoundation.h>

@interface TtsSynthesizer : NSObject <RCTBridgeModule>
@end

@implementation TtsSynthesizer

RCT_EXPORT_MODULE()

+ (BOOL)requiresMainQueueSetup {
  return YES;
}

RCT_EXPORT_METHOD(generate:(NSString *)text
                  resolve:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject)
{
  AVSpeechUtterance *utterance = [[AVSpeechUtterance alloc] initWithString:text];
  utterance.voice = [AVSpeechSynthesisVoice voiceWithLanguage:@"en-US"];
  utterance.rate = AVSpeechUtteranceDefaultSpeechRate;

  AVSpeechSynthesizer *synth = [[AVSpeechSynthesizer alloc] init];

  NSMutableData *floatData = [NSMutableData data];
  __block double sourceSampleRate = 22050.0;
  __block BOOL resolved = NO;

  [synth writeUtterance:utterance toBufferCallback:^(AVAudioBuffer *buffer) {
    /* Reference synth inside the block so ARC keeps it alive
       until the callback sequence completes (final empty buffer). */
    (void)synth;

    AVAudioPCMBuffer *pcm = (AVAudioPCMBuffer *)buffer;

    if (pcm.frameLength == 0) {
      /* Empty buffer = end of speech. Convert and return. */
      if (resolved) return;
      resolved = YES;

      if (floatData.length == 0) {
        resolve(@"");
        return;
      }

      /* Downsample from source rate to 8kHz, convert float -> uint8 */
      const float *samples = (const float *)floatData.bytes;
      NSUInteger sampleCount = floatData.length / sizeof(float);
      double targetRate = 8000.0;
      double ratio = sourceSampleRate / targetRate;
      NSUInteger outputLength = (NSUInteger)(sampleCount / ratio);

      NSMutableData *output = [NSMutableData dataWithLength:outputLength];
      uint8_t *outBytes = (uint8_t *)output.mutableBytes;

      for (NSUInteger i = 0; i < outputLength; i++) {
        NSUInteger srcIdx = (NSUInteger)(i * ratio);
        if (srcIdx >= sampleCount) srcIdx = sampleCount - 1;

        /* Clamp to [-1.0, 1.0] then map to [0, 255]. Silence = 128. */
        float s = samples[srcIdx];
        if (s < -1.0f) s = -1.0f;
        if (s > 1.0f) s = 1.0f;
        outBytes[i] = (uint8_t)(((s + 1.0f) * 127.5f) + 0.5f);
      }

      NSString *base64 = [output base64EncodedStringWithOptions:0];
      resolve(base64);
      return;
    }

    /* Accumulate float samples from this chunk */
    sourceSampleRate = pcm.format.sampleRate;

    if (pcm.floatChannelData) {
      const float *channelData = pcm.floatChannelData[0]; /* mono: channel 0 */
      [floatData appendBytes:channelData length:pcm.frameLength * sizeof(float)];
    }
  }];
}

@end
