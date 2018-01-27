#include "global.h"
#include "NotesLoaderMID.h"
#include "RageFile.h"
#include "TimingData.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "NoteData.h"
#include "Song.h"
#include "Steps.h"
#include "GameManager.h"
#include "Difficulty.h"
#include "IniFile.h"
#include "MidiFile.h"

/**
 * Midi Notes Loader
 * Designed to load note data directly from the midi files used in Guitar Hero and RockBand
 * It may be possible to load note data for other game modes, but that's for another time
 */

void MIDILoader::GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out )
{
   GetDirListing( sPath + std::string("*.mid"), out );
}

// TODO: implement this
bool MIDILoader::LoadFromDir( const std::string &sDir, Song &out ) {
   LOG->Trace( "MIDILoader::LoadFromDir(%s)", sDir.c_str() );
   return false;
}

// TODO: also implement this
bool MIDILoader::LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out ) {
   return false;
}
