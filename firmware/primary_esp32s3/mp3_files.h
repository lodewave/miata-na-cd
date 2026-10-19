// ---------------------------------------------------------------------------
// mp3_files.h — which files on the SD card the MP3 player plays
//
// Shared by the MP3 player (mp3player.ino) and the function test's SD card
// test, so both use the same rules. mp3WalkCard() (mp3player.ino) goes
// through the root and its folders and reports every entry:
//   - tracks: name ends in .mp3 (any case), not hidden
//   - folders are entered up to MP3_MAX_DEPTH levels below the root
//   - skipped: hidden folders/files (".", including macOS "._x.mp3" and
//     ".Trashes"), "System Volume Information", "$RECYCLE.BIN", and folders
//     deeper than MP3_MAX_DEPTH
// Order is the card's directory order (the player shuffles anyway).
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

#define MP3_MAX_FILES     500   // tracks kept; full paths in RAM (~35 KB at 500)
#define MP3_MAX_DEPTH     4     // folder levels below the root, e.g. /A/B/C/D/x.mp3
#define SD_MAX_OPEN_FILES 8     // SD.begin(): walk holds up to MP3_MAX_DEPTH + 1
                                // folders open, plus the track being played

enum Mp3Entry {
  MP3_TRACK,           // playable MP3
  MP3_FOLDER,          // folder that is searched
  MP3_SKIPPED_FOLDER,  // hidden / system folder, not searched
  MP3_TOO_DEEP,        // folder deeper than MP3_MAX_DEPTH, not searched
  MP3_SKIPPED_FILE,    // hidden file ending in .mp3 (e.g. macOS "._x.mp3")
  MP3_OTHER_FILE       // anything else
};

// Called for every entry. path is the full path ("/Album/01 Song.mp3"),
// size is 0 for folders, depth 0 = root. Return false to stop the walk.
typedef bool (*Mp3WalkFn)(const String& path, Mp3Entry kind, uint32_t size,
                          int depth, void* ctx);

// Walk the mounted SD card from "/" (mp3player.ino)
void mp3WalkCard(Mp3WalkFn fn, void* ctx);
