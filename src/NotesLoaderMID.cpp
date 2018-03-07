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
// chord, and forced note rules are different for each
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

// struct of data to pass back and forth when adding guitar mode notes
struct GuitarData {
   int *iPrevNoteMark;
   int iPrevNoteTrack;
   int iLastForcedRow;
   int iLastTapRow;
   int iLastChordRow;
   int iResolution;
   int iHopoResolution;
   int iCols;
   HOPORules hopoRules;
};

// Prepares a struct of guitar data to be used
GuitarData getNewGuitarData(int resolution, int hopoResolution, int cols, HOPORules rules)
{
   GuitarData gd;
   gd.iPrevNoteMark = (int*)malloc(cols * sizeof(int));
   
   for(int i=0; i<cols; i++) {
      gd.iPrevNoteMark[i] = -1;
   }
   gd.iPrevNoteTrack = -1;
   gd.iLastForcedRow = -1;
   gd.iLastTapRow = -1;
   gd.iLastChordRow = -1;
   gd.iResolution = resolution;
   gd.iHopoResolution = hopoResolution;
   gd.iCols = cols;
   gd.hopoRules = rules;
   
   return gd;
}

// Delete the GuitarData struct to prevent memory leaks
void deleteGuitarData(GuitarData &gd) {
   // just free the one array, rest should be cleaned up automatically
   free(gd.iPrevNoteMark);
}

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
   mo.bassTrack = NULL;
   mo.guitarTrack = NULL;
   mo.drumTrack = NULL;
   mo.vocalTrack = NULL;
   mo.eventTrack = NULL;
   mo.beatTrack = NULL;
   mo.venueTrack = NULL;
   mo.otherTrack = NULL;
   
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
               // just in case it's the beat track with the song title first
               tempTrk = tempTrk->pNext;
            }
         }
         if(tempTrk->subType == MidiFile::MidiMeta_Tempo || tempTrk->subType == MidiFile::MidiMeta_TimeSignature)
         {
            // this is the beat/tempo track
            mo.beatTrack = tempTrk;
         }
      }
   }
   
   // default to GH HOPO rules
   if(mo.HOPOType == Unknown_Rules) mo.HOPOType = GH_HOPO_Rules;
   
   return mo;
}

void getNoteRangeForDifficulty(Difficulty diff, int* lower, int* upper)
{
   switch(diff)
   {
      case Difficulty_Easy:
         *lower = 60;
         *upper = 66;
         break;
      case Difficulty_Medium:
         *lower = 72;
         *upper = 78;
         break;
      case Difficulty_Hard:
         *lower = 84;
         *upper = 90;
         break;
      case Difficulty_Challenge:
         *lower = 96;
         *upper = 102;
         break;
   }
}

// noteCategory (selected based on duration)
//    1 - tap/hold
//    2 - gem/gemhold
//    3 - hopo/hopohold
void placeNote(NoteData &notes, int track, int start, int end, int noteCategory)
{
   TapNote singleTapKind;
   TapNote heldTapKind;
   
   if(noteCategory == 1) {
      singleTapKind = TAP_ORIGINAL_TAP;
      heldTapKind = TAP_ORIGINAL_HOLD_HEAD;
   } else if(noteCategory == 2) {
      singleTapKind = TAP_ORIGINAL_GEM;
      heldTapKind = TAP_ORIGINAL_GEM_HOLD;
   } else if(noteCategory == 3) {
      singleTapKind = TAP_ORIGINAL_HOPO;
      heldTapKind = TAP_ORIGINAL_HOPO_HOLD;
   } else {
      singleTapKind = TAP_EMPTY;
      heldTapKind = TAP_EMPTY;
   }
   
   if( end > start )
   {
      notes.AddHoldNote(track, start, end, heldTapKind);
   } else {
      notes.SetTapNote(track, start, singleTapKind);
   }
}

// Helper function to determine if a note is a hopo or not because logic is messy
bool checkHOPOConditions(int iNoteTrack, int iNoteMark, GuitarData gd)
{
   bool ShouldBeHOPO = false;
   int k = 0;
   int prevNoteTrack = -1;
   int prevNoteMark = -1;
   
   // quick check rules
   // if this note is in a chord, then HOPO=no and that's that. No forcing applies.
   if( gd.iLastChordRow == iNoteMark && gd.iLastChordRow != -1 )
   {
      return false;
   }
   
   // In rockband, the tap force column denotes a note should NOT be a HOPO
   if( gd.iLastTapRow == iNoteMark && gd.hopoRules == RB_HOPO_Rules )
   {
      return false;
   }
   
   while( k<gd.iCols )
   {
      // difference is less than hopo resolution, notes on different tracks, and it's not the 1st note, HOPO=yes
      if(iNoteMark - gd.iPrevNoteMark[k] <= gd.iHopoResolution &&
         iNoteTrack != k &&
         gd.iPrevNoteMark[k] != -1)
      {
         ShouldBeHOPO = true;
      }
      
      if(gd.iPrevNoteMark[k] > prevNoteMark)
      {
         prevNoteMark = gd.iPrevNoteMark[k];
         prevNoteTrack = k;
      }
      
      ++k;
   }
   
   // if this uses RB hopo rules, and is following a chord of which this row was a part of, it is definitely not a HOPO
   if(gd.hopoRules == RB_HOPO_Rules &&
      prevNoteTrack != -1 &&
      gd.iPrevNoteMark[iNoteTrack] != -1 &&
      prevNoteMark != -1 &&
      gd.iPrevNoteMark[iNoteTrack] == prevNoteMark )
   {
      ShouldBeHOPO = false;
   }
   
   // Reverse the note if the row was marked to be forced (GH)
   // or force it to be always HOPO (RB)
   if( gd.iLastForcedRow == iNoteMark && gd.iLastForcedRow != -1 )
   {
      if(gd.hopoRules == RB_HOPO_Rules) ShouldBeHOPO = true;
      else ShouldBeHOPO = !ShouldBeHOPO;
   }
   
   return ShouldBeHOPO;
}

// Adds a note to the note data based on Guitar Hero/RockBand rules
void addGHRBNote(NoteData &notes, int col, int start, int end, GuitarData &gd)
{
   bool isChordRow = false;
   TapNote taps[gd.iCols];
   int highestNote = -1;
   // precalculate these
   int realEnd = end;
   // if the duration <= 1/2 resolution, it's not held
   // and if it is held, shorten the duration slightly so as not to overrun other notes
   if(end - start > gd.iResolution / 2)
   {
      realEnd -= gd.iResolution / 8;
   } else {
      realEnd = start;
   }
   int startRow = BeatToNoteRow((float)start/gd.iResolution);
   int endRow = BeatToNoteRow((float)realEnd/gd.iResolution);
   // get all notes on current row, in case any need to be replaced
   for(int i=0; i<gd.iCols; i++)
   {
      taps[i] = notes.GetTapNote(i, startRow);
      if(taps[i] != TAP_EMPTY) {
         isChordRow = true;
         if(i > highestNote) highestNote = i;
      }
   }
   
   // determine if this is a forced note (on idx cols)
   if(col == gd.iCols - 1)
   {
      // check if any other notes were placed on this row
      if(isChordRow)
      {
         // if there are multiple notes on a forced row, delete all but the highest
         bool foundHighest = false;
         for(int i=gd.iCols-1; i>=0; i--)
         {
            if(taps[i] == TAP_EMPTY) continue;
            // already found the one we want, delete this note
            if(foundHighest)
            {
               notes.SetTapNote(i, startRow, TAP_EMPTY);
            } else { // else, replace the other note with the opposite type
               foundHighest = true;
               bool wasHopo = (taps[i].type == TapNoteType_HOPO || taps[i].type == TapNoteType_HOPOHold) &&
                              gd.hopoRules == GH_HOPO_Rules;
               placeNote(notes, i, startRow, startRow + taps[i].iDuration, wasHopo ? 2 : 3);
            }
         }
      }
      // mark last forced note row
      gd.iLastChordRow = start;
   }
   else if(col >= gd.iCols) // this row mean not a hopo in RB, or add a tap note in GH
   {
      // check if any other notes were placed on this row
      if(isChordRow)
      {
         // replace any notes on this row with taps/holds
         for(int i=0; i<gd.iCols; i++)
         {
            if(taps[i] == TAP_EMPTY) continue;
            placeNote(notes, i, startRow, startRow + taps[i].iDuration,
                      gd.hopoRules == RB_HOPO_Rules ? 2 : 1);
         }
      }
      // mark last normal note row
      gd.iLastTapRow = start;
   }
   else // this is a normal note
   {
      // place a tap/hold if this row is a tap row
      if(start == gd.iLastTapRow)
      {
         placeNote(notes, col, startRow, endRow, gd.hopoRules == RB_HOPO_Rules ? 2 : 1);
      }
      else
      {
         // if this row forms a chord
         if(isChordRow)
         {
            // get if the last note was a hopo, should only be relevant for 1 note in row so this'll work
            bool wasHopo = (taps[highestNote].type == TapNoteType_HOPO || taps[highestNote].type == TapNoteType_HOPOHold) ||
                           gd.hopoRules == RB_HOPO_Rules;
            // check if this row was forced
            if(start == gd.iLastForcedRow)
            {
               // check if this is the lowest note (only the highest will be saved)
               if(col < highestNote)
               {
                  // do not place new note or modify anything, this note does not exist
                  return;
               }
               else
               {
                  // assume hopo rules were done for the last note & correct
                  // remove the last note and place this new one
                  notes.SetTapNote(highestNote, startRow, TAP_EMPTY);
                  placeNote(notes, col, startRow, endRow, wasHopo ? 3 : 2);
               }
            }
            else
            {
               // if the last note was a hopo, replace it with a gem
               if(wasHopo)
               {
                  placeNote(notes, highestNote, startRow, startRow + taps[highestNote].iDuration, 2);
               }
               // place a gem for the latest note
               placeNote(notes, col, startRow, endRow, 2);
            }
            // mark that a chord happened here
            gd.iLastChordRow = start;
         }
         else
         {
            // figure out what type it should be and place it
            bool isHopo = checkHOPOConditions(col, start, gd);
            placeNote(notes, col, startRow, endRow, isHopo ? 3 : 2);
         }
      } // end else !(start == lastTapRow)
      
      // set the information about the note just added
      gd.iPrevNoteMark[col] = start;
      gd.iPrevNoteTrack = col;
   } // end else normal note col
}

/**
 * Gets the notes from a midi track based on the standard rules of Guitar Hero/Rock Band
 * cols parameter is just in case drums or six-fret can be loaded in the future
 */
NoteData getGHRBNotesFromTrack(MidiFile::MidiEvent* track, Difficulty diff, HOPORules rules, int resolution, int HOPOres, int cols)
{
   NoteData newNotes;
   MidiFile::MidiEvent* curEvt = track;
   // +1 to account for forced hopo and tap tracks
   std::vector<MidiFile::MidiEvent_Note*> notesInProgress(cols+1);
   // note bounds for difficulty
   int low = 0;
   int high = 0;
   
   getNoteRangeForDifficulty(diff, &low, &high);
   newNotes.SetNumTracks(cols);
   for(int i=0; i<cols+1; i++)
   {
      notesInProgress[i] = NULL;
   }
   
   GuitarData gd = getNewGuitarData(resolution, HOPOres, cols, rules);
   
   // While there are more events
   while(curEvt)
   {
      if(curEvt->type == MidiFile::MidiEventType_Note)
      {
         MidiFile::MidiEvent_Note* tempNote = (MidiFile::MidiEvent_Note*) curEvt;
         int idx = tempNote->note - low;
         // Is this note in the range we care about?
         if(idx >= 0 && tempNote->note <= high)
         {
            if(tempNote->subType == MidiFile::MidiNote_NoteOn)
            {
               if(notesInProgress[idx] == NULL)
                  notesInProgress[idx] = tempNote;
            }
            else if(tempNote->subType == MidiFile::MidiNote_NoteOff)
            {
               // close off any note in progress and place either a hold or tap note
               if(notesInProgress[idx])
               {
                  int start = notesInProgress[idx]->tick;
                  int end = tempNote->tick;
                  
                  // quick sanity check...
                  if(end > start)
                  {
                     // add the note, this is actually a weirdly complex process to do...
                     addGHRBNote(newNotes, idx, start, end, gd);
                     notesInProgress[idx] = NULL;
                  }
               }
            }
         }
      }
      
      curEvt = curEvt->pNext;
   }
   
   // bit of cleanup
   deleteGuitarData(gd);
   return newNotes;
}

/**
 * Gets note data in a generic way (modulus the note number by number of columns)
 */
NoteData getGenericNotesFromTrack(MidiFile::MidiEvent* track, int resolution, int cols)
{
   NoteData newNotes;
   MidiFile::MidiEvent* curEvt = track;
   std::vector<MidiFile::MidiEvent_Note*> notesInProgress(cols);
   
   newNotes.SetNumTracks(cols);
   for(int i=0; i<cols; i++)
   {
      notesInProgress[i] = NULL;
   }
   
   while(curEvt)
   {
      if(curEvt->type == MidiFile::MidiEventType_Note)
      {
         MidiFile::MidiEvent_Note* tempNote = (MidiFile::MidiEvent_Note*) curEvt;
         int idx = tempNote->channel % cols;
         if(tempNote->subType == MidiFile::MidiNote_NoteOn)
         {
            // save this to be processed later, 2 noteOn events in a row will take the first
            if(notesInProgress[idx] == NULL)
               notesInProgress[idx] = tempNote;
         }
         else if(tempNote->subType == MidiFile::MidiNote_NoteOff)
         {
            // close off any note in progress and place either a hold or tap note
            if(notesInProgress[idx])
            {
               int start = BeatToNoteRow((float)notesInProgress[idx]->tick/resolution);
               int end = BeatToNoteRow((float)tempNote->tick/resolution);
               
               // 240 is the magic number for 480 resolution, so...
               if(end > start)
               {
                  if(end - start < 24)
                  {
                     newNotes.AddHoldNote(idx, start, end, TAP_ORIGINAL_HOLD_HEAD);
                  }
                  else
                  {
                     newNotes.SetTapNote(idx, start, TAP_ORIGINAL_TAP);
                  }
                  
                  notesInProgress[idx] = NULL;
               }
            }
         }
      }
      curEvt = curEvt->pNext;
   }
   
   return newNotes;
}

// parses the bpm and timesignature data to timing data
void parseBeatTrack(TimingData &td, MidiFile::MidiEvent* track, int resolution)
{
   MidiFile::MidiEvent* curEvent = track;
   
   // some default segments
   td.set_offset( 0 );
   td.AddSegment(ComboSegment(0.0,1,1));
   td.AddSegment(ScrollSegment(0.0,1.0));
   td.AddSegment(TickcountSegment(0, 48));
   
   // while there's still more events
   while(curEvent) {
      // only care about meta types
      if(curEvent->type == MidiFile::MidiEventType_Meta)
      {
         if(curEvent->subType == MidiFile::MidiMeta_Tempo)
         {
            MidiFile::MidiEvent_Tempo* tempEvent = (MidiFile::MidiEvent_Tempo*) curEvent;
            td.AddSegment( BPMSegment(BeatToNoteRow((float)tempEvent->tick/resolution), tempEvent->BPM ));
         }
         else if(curEvent->subType == MidiFile::MidiMeta_TimeSignature)
         {
            MidiFile::MidiEvent_TimeSignature* tsEvent = (MidiFile::MidiEvent_TimeSignature*) curEvent;
            int num = (int)tsEvent->numerator;
            int den = (int)tsEvent->denominator;
            // just being safe, testing found some weird stuff
            if(num == 0) num = 4;
            if(den == 0) den = 4;
            td.AddSegment( TimeSignatureSegment(BeatToNoteRow((float)tsEvent->tick / resolution), num, den) );
         }
      }
      
      curEvent = curEvent->pNext;
   }
}

// parses event titles to be added to events in the song
void parseEventTrack(TimingData &td, MidiFile::MidiEvent *track, int resolution)
{
   MidiFile::MidiEvent* curEvent = track;
   
   // while there's still more events
   while(curEvent) {
      // only care about meta types
      if(curEvent->type == MidiFile::MidiEventType_Meta)
      {
         // tecnically there's a lot on the event track, but lets only get labels
         if(curEvent->subType == MidiFile::MidiMeta_Text)
         {
            MidiFile::MidiEvent_Text* txtEvent = (MidiFile::MidiEvent_Text*) curEvent;
            std::string txt = std::string(txtEvent->buffer);
            td.AddSegment( LabelSegment(BeatToNoteRow((float)txtEvent->tick/resolution), txt ));
         }
      }
      
      curEvent = curEvent->pNext;
   }
}

// parses an ini file, if it exists, to get song metadata
void parseINI(std::string sFilePath, int* resolution, int* hopoResolution, std::string &title,
              std::string &artist, std::string &charter)
{
   IniFile ini;
   if( !ini.ReadFile( sFilePath + "song.ini" ) )
   {
      // could not find ini file, oh well
      *hopoResolution = *resolution / 4;
   } else
   {
      bool eightNoteHopo = false;
      int hopoRes = 2;
      
      // first, check if there's any special HOPO frequency because FoFiX cheats
      if( ini.GetValue("song", "hopofreq", hopoRes) ) {
         switch( hopoRes ){
               // example hopoResolutions given if resolution is 480
            case 0:
               // fewest HOPOs
               *hopoResolution = *resolution / 2; // ~= 240
               break;
            case 1:
               // few HOPOs
               *hopoResolution = *resolution * (3 / 8);// ~= 180
               break;
            case 2:
            default:
               // normal HOPOs
               *hopoResolution = *resolution / 4; // ~= 120 standard
               break;
            case 3:
               // more HOPOs
               *hopoResolution = *resolution * (3 / 16);// ~= 90
               break;
            case 4:
               // most HOPOs
               *hopoResolution = *resolution / 8; // ~= 60
               break;
               // song.ini details say this can go up to 5, but FoFiX source code says otherwise
         }
      }
      // next, if eighthnotes count as hopos, divide the hopoResolution in half
      if( ini.GetValue("song", "eighthnote_hopo", eightNoteHopo) ) {
         if( eightNoteHopo ) *hopoResolution /= 2;
      }
      // last, get the artist and song names
      if( !ini.GetValue("song", "artist", artist)) {
         artist = "";
      }
      if( !ini.GetValue("song", "name", title)) {
         title = "";
      }
      if( !ini.GetValue("song", "frets", charter)) {
         charter = "";
      }
   }
}

// Gets the music files and tracks in the current directory
void getMusicFiles( const std::string path, Song &out )
{
   // get all .ogg files
   std::vector<std::string> songFiles;
   GetDirListing( path + std::string("*.ogg"), songFiles );
   
   // if only one file, set it as music file
   if( songFiles.size() == 1 )
   {
      out.m_sMusicFile = path + songFiles[0];
   } else {
      for(int i=songFiles.size() - 1; i >= 0; i--)
      {
         if(songFiles[i].compare("guitar.ogg"))
         {
            out.m_sInstrumentTrackFile[InstrumentTrack_Guitar] = path + songFiles[i];
         } else if(songFiles[i].compare("song.ogg"))
         {
            out.m_sInstrumentTrackFile[InstrumentTrack_Rhythm] = path + songFiles[i];
         } else if(songFiles[i].compare("rhythm.ogg"))
         {
            out.m_sInstrumentTrackFile[InstrumentTrack_Bass] = path + songFiles[i];
         }
      }
   }
   // I really hope this works, don't want to make this more complicated
}

std::string getTimeString(float fSeconds)
{
   int min = ((int)fSeconds) / 60;
   int sec = ((int)fSeconds) % 60;
   int csc = (int)((fSeconds - (sec + (60 * min))) * 100); // centiseconds
   std::string minStr = std::to_string(min);
   std::string secStr = std::to_string(sec);
   std::string cscStr = std::to_string(csc);
   if(minStr.size() == 1) minStr = '0' + minStr;
   if(secStr.size() == 1) secStr = '0' + secStr;
   if(cscStr.size() == 1) cscStr = '0' + cscStr;
   return "[" + minStr + ":" + secStr + "." + cscStr + "]";
}

// Creates a lyrics file given the vocal track from the midifile and returns its location
std::string createLyricsFile( const std::string path, TimingData td, int resolution, MidiFile::MidiEvent *track)
{
   std::string lrcFileName = path + "lyrics.lrc";
   RageFile f;
   MidiFile::MidiEvent *curEvt = track;
   int lastMeasure = 0;
   std::string curLine = "";
   
   if( !f.Open(lrcFileName, RageFile::WRITE) )
   {
      LOG->UserLog( "Lyrics file at", path, "couldn't be opened for writing: %s", f.GetError().c_str() );
      return "";
   }
   
   // format for .lrc files:
   // [mm:ss.xx]Lyrics here
   // [mm:ss.xx]More lyrics
   // divide by measure, time the lyrics to the first lyric of the measure
   while(curEvt)
   {
      // only care about lyrics
      if(curEvt->type == MidiFile::MidiEventType_Meta)
      {
         if(curEvt->subType == MidiFile::MidiMeta_Lyric || curEvt->subType == MidiFile::MidiMeta_Text)
         {
            MidiFile::MidiEvent_Text* txtEvent = (MidiFile::MidiEvent_Text*) curEvt;
            std::string txt = std::string(txtEvent->buffer);
            
            // '+' is used to carry a lyric through pitch changes, '[' is for action markers
            if(txt.at(0) != '+' && txt.at(0) != '[')
            {
               // strip special characters
               if(txt.back() == '#') txt = txt.substr(0, txt.size() - 1); // for spoken words
               if(txt.back() == '^') txt = txt.substr(0, txt.size() - 1); // ???
               
               // if this is a new measure, append the last line and start another, but don't chop words
               int newMeasure = (int)(txtEvent->tick / resolution) / 4;
               if(lastMeasure < newMeasure && curLine.back() != '-')
               {
                  f.PutLine(curLine);
                  if(newMeasure - lastMeasure > 2)
                  {
                     // add a blank line when 2 or more measures have no lyrics
                     std::string blankLine = getTimeString(td.GetElapsedTimeFromBeat((float)((lastMeasure+1) * 4)));
                     f.PutLine(blankLine);
                  }
                  curLine = getTimeString(td.GetElapsedTimeFromBeat( (float)txtEvent->tick / resolution ));
                  lastMeasure = newMeasure;
               }
               
               // '-' is used to connect syllables inside words
               if(curLine.back() == '-') {
                  curLine = curLine.substr(0, curLine.size() - 1);
               } else {
                  curLine += ' ';
               }
               
               // append to line
               curLine += txt;
            }
         }
      }
      
      curEvt = curEvt->pNext;
   }
   
   f.PutLine(curLine);
   if( f.Flush() == -1 )
      return "";
   
   return lrcFileName;
}

void MIDILoader::GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out )
{
   GetDirListing( sPath + std::string("*.mid"), out );
}

// TODO: set important data for steps/song in here
bool MIDILoader::LoadFromDir( const std::string &sDir, Song &out ) {
   LOG->Trace( "MIDILoader::LoadFromDir(%s)", sDir.c_str() );
   
   // Get the mid file to load
   std::vector<std::string> arrayMidiFileNames;
   GetDirListing( sDir + std::string("*.mid"), arrayMidiFileNames );
   // We shouldn't have been called to begin with if there were no files.
   ASSERT( arrayMidiFileNames.size() != 0 );
   std::string dir = out.GetSongDir();
   
   // Parse the midi file
   MidiFile *mf = ReadMidiFile(dir+arrayMidiFileNames[0]);
   
   // Organize the midi
   MidiOrganizer mo = organizeMidi(mf);
   
   // Parse meta info
   int resolution = mf->ticksPerBeat;
   int hopoResolution = resolution / 4;
   std::string title = "";
   std::string artist = "";
   std::string charter = "";
   parseINI(dir, &resolution, &hopoResolution, title, artist, charter);
   
   out.m_sMainTitle = title;
   out.m_sArtist = artist;
   out.m_sCredit = charter;
   getMusicFiles(sDir, out);
   parseBeatTrack(out.m_SongTiming, mo.beatTrack, resolution);
   if(mo.eventTrack != NULL) parseEventTrack(out.m_SongTiming, mo.eventTrack, resolution);
   
   // get lyrics file
   std::vector<std::string> lyricFiles;
   std::string lrcFile = "";
   GetDirListing( sDir + std::string("*.lrc"), lyricFiles );
   if( lyricFiles.size() == 0 && mo.vocalTrack != NULL)
   {
      lrcFile = createLyricsFile(sDir, out.m_SongTiming, resolution, mo.vocalTrack);
   } else {
      lrcFile = sDir + lyricFiles[0];
   }
   if(!lrcFile.empty()) out.m_sLyricsFile = lrcFile;
   
   // Get all notes from the guitar track
   for(int i=0; i<4; i++)
   {
      // TODO: Initialize data and stuff in these steps
      Steps* newSteps = out.CreateSteps();
      /* steps initialization stuff */
      newSteps->m_StepsType = StepsType_guitar_solo;
      newSteps->SetChartStyle("Guitar");
      newSteps->SetCredit( charter );
      newSteps->SetDescription( charter );
      // out.SetMusicFile( headerInfo[2] ); // TODO: music stuff needed?
      newSteps->SetMeter(1); // will have to try this later, has a history of crashing when not hardcoded
      newSteps->SetSavedToDisk(true);
       
      newSteps->SetFilename(dir+arrayMidiFileNames[0]);
      switch(i) {
         case 0:
            newSteps->SetDifficulty(Difficulty_Easy);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Easy, mo.HOPOType,
                                                        resolution, hopoResolution, 6));
            break;
         case 1:
            newSteps->SetDifficulty(Difficulty_Medium);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Medium, mo.HOPOType,
                                                        resolution, hopoResolution, 6));
            break;
         case 2:
            newSteps->SetDifficulty(Difficulty_Hard);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Hard, mo.HOPOType,
                                                        resolution, hopoResolution, 6));
            break;
         case 3:
            newSteps->SetDifficulty(Difficulty_Challenge);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Challenge, mo.HOPOType,
                                                        resolution, hopoResolution, 6));
            break;
         default:
            break;
      }
      newSteps->TidyUpData();
      out.AddSteps(newSteps);
   }
   
   // Put all the data together
   out.TidyUpData();
   
   return true;
}

// TODO: set important data in here
bool MIDILoader::LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out ) {
   // Parse the midi file
   MidiFile *mf = ReadMidiFile(cachePath);
   
   // Organize the midi
   MidiOrganizer mo = organizeMidi(mf);
   
   // Get the HOPO resolution
   std::string sBasePath = cachePath.substr(0, cachePath.find_last_of("/")+1);
   int resolution = mf->ticksPerBeat;
   int hopoResolution = resolution / 4;
   std::string title = "";
   std::string artist = "";
   std::string charter = "";
   parseINI(sBasePath, &resolution, &hopoResolution, title, artist, charter);
   
   // Get the desired notes from the guitar track
   out.SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, out.GetDifficulty(), mo.HOPOType, resolution, hopoResolution, 6));
   out.TidyUpData();
   
   return true;
}
