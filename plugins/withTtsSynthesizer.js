/**
 * Expo config plugin that adds the TtsSynthesizer native iOS module
 * to the Xcode project during EAS Build.
 *
 * The module is pure Objective-C (no Swift) so no bridging header is needed.
 * It uses AVSpeechSynthesizer to generate 8-bit PCM audio from text.
 */

const { IOSConfig } = require("@expo/config-plugins");
const path = require("path");
const fs = require("fs");

const withBuildSourceFile = IOSConfig.XcodeProjectFile.withBuildSourceFile;

function withTtsSynthesizer(config) {
  // Source file lives alongside this plugin (ios/ is gitignored,
  // so EAS Build wouldn't see it there).
  const objcContents = fs.readFileSync(
    path.join(__dirname, "TtsSynthesizer.m"),
    "utf8"
  );

  // Add the ObjC file to the Xcode project
  config = withBuildSourceFile(config, {
    filePath: "TtsSynthesizer.m",
    contents: objcContents,
    overwrite: true,
  });

  return config;
}

module.exports = withTtsSynthesizer;
