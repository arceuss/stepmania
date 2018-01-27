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

// references:
//http://rockband.scorehero.com/forum/viewtopic.php?t=1711
//http://www.scorehero.com/forum/viewtopic.php?t=1179

// simple enum for if this midi is using Guitar Hero or Rock Band HOPO rules
// In rockband, HOPOs can't occur after a chord if the note was part of the
// chord, and in guitar hero, forced chords are possible (but not in RB)
enum HOPORules {
   GH_HOPO_Rules,
   RB_HOPO_Rules,
   Unknown_Rules
};

// helper for organizing midi data so no guessing which track is which
struct MidiOrganizer {
   MidiFile *midFile;                    // Original file
   MidiFile::MidiEvent *beatTrack;    // Track for BPM and time signature changes
   MidiFile::MidiEvent *guitarTrack;  // Guitar note track
   MidiFile::MidiEvent *bassTrack;    // Bass/player2 note track
   MidiFile::MidiEvent *drumTrack;    // Drum note track
   MidiFile::MidiEvent *vocalTrack;   // Vocals track (includes lyrics)
   MidiFile::MidiEvent *eventTrack;   // General events track
   MidiFile::MidiEvent *venueTrack;   // Track for syncing stage events
   MidiFile::MidiEvent *otherTrack;   // Other unknown/uneeded track
   HOPORules HOPOType;                // Which hopo rule set the song uses
};

// compares a c_str to an actual string because reasons
bool compareToString(char* cstr, std::string str)
{
   size_t len = str.size();
   
   for(int i = 0; i < len; i++)
   {
      if(cstr[i] != str.at(i))
         return false;
   }
   // c string must also end with a null terminator (same length)
   return cstr[len] == '\0';
}

MidiOrganizer organizeMidi(MidiFile* mf)
{
   MidiOrganizer mo;
   mo.HOPOType = Unknown_Rules;
   mo.midFile = mf;
   
   for(int i = mf->numTracks - 1; i >= 0; i--)
   {
      MidiFile::MidiEvent* tempTrk = mf->tracks[i];
      if(tempTrk->type == MidiFile::MidiEventType_Meta)
      {
         if(tempTrk->subType == MidiFile::MidiMeta_TrackName)
         {
            // found one of the important tracks, figure out which it is
            MidiFile::MidiEvent_Text* tempTxt = (MidiFile::MidiEvent_Text*) tempTrk;
            if(compareToString(tempTxt->buffer, "PART GUITAR")) {
               mo.guitarTrack = tempTrk;
            } else if(compareToString(tempTxt->buffer, "PART BASS")) {
               mo.bassTrack = tempTrk;
            }
            // Drums and vocals means this is a RB song
            // AFAIK anything past GH3 never had midis ripped because people thought
            // the new features would screw with existing midi parsers
            else if(compareToString(tempTxt->buffer, "PART DRUMS")) {
               mo.drumTrack = tempTrk;
               if(mo.HOPOType == Unknown_Rules) mo.HOPOType = RB_HOPO_Rules;
            } else if(compareToString(tempTxt->buffer, "PART VOCALS")) {
               mo.vocalTrack = tempTrk;
               if(mo.HOPOType == Unknown_Rules) mo.HOPOType = RB_HOPO_Rules;
            }
            // These will probably never be used, but save them just in case
            else if(compareToString(tempTxt->buffer, "EVENTS")) {
               mo.eventTrack = tempTrk;
            }  else if(compareToString(tempTxt->buffer, "VENUE")) {
               mo.venueTrack = tempTrk;
            } else {
               // dunno what this is, save the last of the unknowns at least
               mo.otherTrack = tempTrk;
            }
         }
         else if(tempTrk->subType == MidiFile::MidiMeta_Tempo || tempTrk->subType == MidiFile::MidiMeta_TimeSignature)
         {
            // this is the beat/tempo track
            mo.beatTrack = tempTrk;
         }
      }
   }
   
   // default to GH HOPO rules
   if(mo.HOPOType = Unknown_Rules) mo.HOPOType = GH_HOPO_Rules;
   
   return mo;
}

/**
 * Gets the notes from a midi track based on the standard rules of Guitar Hero/Rock Band
 * cols parameter is just in case drums or six-fret can be loaded in the future
 */
NoteData getGHRBNotesFromTrack(MidiFile::MidiEvent* track, Difficulty diff, HOPORules rules, int resolution, int HOPOres, int cols)
{
   NoteData newNotes;
   
   return newNotes;
}

/**
 * Gets note data in a generic way (modulus the note number by number of columns)
 */
NoteData getGenericNotesFromTrack(MidiFile::MidiEvent* track, int resolution, int cols)
{
   NoteData newNotes;
   
   return newNotes;
}

// parses the bpm and timesignature data to timing data
void parseBeatTrack(TimingData* td, MidiFile::MidiEvent* track, int resolution)
{
   
}

void MIDILoader::GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out )
{
   GetDirListing( sPath + std::string("*.mid"), out );
}

// TODO: implement this
bool MIDILoader::LoadFromDir( const std::string &sDir, Song &out ) {
   LOG->Trace( "MIDILoader::LoadFromDir(%s)", sDir.c_str() );
   // Parse the midi file
   
   // Find the guitar track
   
   // Get all notes from the guitar track
   
   return false;
}

// TODO: also implement this
bool MIDILoader::LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out ) {
   // Parse the midi file
   
   // Find the guitar track
   
   // Get the desired notes from the guitar track
   
   return false;
}
