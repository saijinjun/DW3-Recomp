// Copying a disc picked through a system file picker: file names from content:// addresses and the cue sheet rewrite.

#include <gtest/gtest.h>

#include "disc_import.h"

using namespace dw3setup;

TEST(DiscImport, GuessesNamesFromPathsAndContentAddresses) {
    EXPECT_EQ(guess_file_name("/sdcard/Download/Digimon World 3 (USA).cue"), "Digimon World 3 (USA).cue");
    EXPECT_EQ(guess_file_name("content://com.android.externalstorage.documents/document/primary%3ADownload%2FDigimon%20World%203%20(USA).bin"),
              "Digimon World 3 (USA).bin");
    EXPECT_EQ(guess_file_name("content://media/external/file/1234"), "1234");
    EXPECT_EQ(guess_file_name("disc.bin"), "disc.bin");
}

TEST(DiscImport, TellsCueSheetsFromSectorData) {
    EXPECT_TRUE(looks_like_cue("FILE \"Game.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"));
    EXPECT_FALSE(looks_like_cue(std::string_view("\0\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\0", 12)));
    EXPECT_FALSE(looks_like_cue("just some text"));
}

TEST(DiscImport, ListsCueFiles) {
    const auto names = cue_file_names("FILE \"A (Track 1).bin\" BINARY\nTRACK 01 MODE2/2352\nFILE b.bin BINARY\nTRACK 02 AUDIO\n");
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "A (Track 1).bin");
    EXPECT_EQ(names[1], "b.bin");
}

TEST(DiscImport, RewritesFileLinesByNameThenPosition) {
    const std::string cue = "FILE \"Game (Track 1).bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
                            "FILE \"Game (Track 2).bin\" BINARY\r\n  TRACK 02 AUDIO\r\n    INDEX 01 00:00:00\r\n";
    std::string out, error;
    // Picked in the other order: matched by name.
    ASSERT_TRUE(rewrite_cue(cue, {"Game (Track 2).bin", "Game (Track 1).bin"}, {"track1.bin", "track2.bin"}, out, error)) << error;
    EXPECT_EQ(cue_file_names(out), (std::vector<std::string>{"track2.bin", "track1.bin"}));
    EXPECT_NE(out.find("TRACK 02 AUDIO"), std::string::npos);
    // Names that say nothing (media store numbers): by position.
    ASSERT_TRUE(rewrite_cue(cue, {"81", "82"}, {"track1.bin", "track2.bin"}, out, error)) << error;
    EXPECT_EQ(cue_file_names(out), (std::vector<std::string>{"track1.bin", "track2.bin"}));
}

TEST(DiscImport, RewriteFailsWhenAFileIsMissing) {
    std::string out, error;
    EXPECT_FALSE(rewrite_cue("FILE a.bin BINARY\nTRACK 01 MODE2/2352\nFILE b.bin BINARY\nTRACK 02 AUDIO\n", {"a.bin"}, {"disc.bin"}, out, error));
    EXPECT_NE(error.find("b.bin"), std::string::npos);
}
