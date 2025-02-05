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
// these sources are only semi accurate, had to do more research on my own

// function that reads a sysex payload and returns what to do with it
// -1 = do nothing
// 1  = tap start
// 2  = tap stop
// 3  = open start
// 4  = open stop
int translateSysex(const char *pData, Difficulty diff)
{
   // sysex payload codes. ask FireFox2000000 about these, I have no idea
   // common header of what we're looking for
   unsigned char commHeader[4] = {0x50, 0x53, 0x00, 0x00};
   // indicates what the event is
   unsigned char tapIndicator[2] = {0xFF, 0x04};
   unsigned char openIndicator = 0x01;
   unsigned char easyIndicator = 0x00;
   unsigned char medIndicator = 0x01;
   unsigned char hardIndicator = 0x02;
   unsigned char expIndicator = 0x03;
   // finals bytes
   unsigned char onIndicator[2] = {0x01, 0xF7};
   unsigned char offIndicator[2] = {0x00, 0xF7};
   
   // ensure the header is valid
   for(int i = 0; i < 4; i++)
   {
      if(pData[i] != commHeader[i]) return -1;
   }
   
   int ret = 0;
   
   // determine the type of message it is and if it fits
   if (pData[4] == tapIndicator[0] && pData[5] == tapIndicator[1])
   {
      ret = 1;
   }
   else if (pData[5] == openIndicator &&
            ((pData[4] == easyIndicator && diff == Difficulty_Easy) ||
            (pData[4] == medIndicator && diff == Difficulty_Medium) ||
            (pData[4] == hardIndicator && diff == Difficulty_Hard) ||
            (pData[4] == expIndicator && diff == Difficulty_Challenge)))
   {
      ret = 3;
   }
   else
   {
      return -1;
   }
   
   // verify the last bytes
   if (pData[7] == onIndicator[1])
   {
      if (pData[6] == offIndicator[0])
         ret += 1;
      else if (pData[6] != onIndicator[0])
         return -1;
   }
   
   return ret;
}

// simple enum for if this midi is using Guitar Hero or Rock Band HOPO rules
// In rockband, HOPOs can't occur after a chord if the note was part of the
// chord, and forced note rules are different for each
enum HOPORules {
   GH_HOPO_Rules,
   RB_HOPO_Rules,
   Unknown_Rules
};

// whether this chart is 6 frets (GHL) or 5
enum ChartFrets {
   FIVE_FRETS,
   SIX_FRETS
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
   ChartFrets FretType;               // Whether to use 5 or 6 frets reading rules
};

// struct of data to pass back and forth when adding guitar mode notes
struct GuitarData {
   int *iPrevNoteMark;
   int iPrevNoteTrack;
   int iLastForcedHOPO;
   int iLastForcedStrum;
   int iLastChordRow;
   int iResolution;
   int iHopoResolution;
   int iCols;
   bool bInTapSection;
   bool bInOpenSection;
   HOPORules hopoRules;
   ChartFrets fretType;
};

// Prepares a struct of guitar data to be used
GuitarData getNewGuitarData(int resolution, int hopoResolution, HOPORules rules, ChartFrets fretType)
{
   GuitarData gd;
   int cols = 0;
   if (fretType == FIVE_FRETS) cols = 6;
   else cols = 7;
   gd.iPrevNoteMark = (int*)malloc(cols * sizeof(int));
   
   for(int i=0; i<cols; i++) {
      gd.iPrevNoteMark[i] = -1;
   }
   gd.iPrevNoteTrack = -1;
   gd.iLastForcedHOPO = -1;
   gd.iLastForcedStrum = -1;
   gd.iLastChordRow = -1;
   gd.iResolution = resolution;
   gd.iHopoResolution = hopoResolution;
   gd.iCols = cols;
   gd.bInTapSection = false;
   gd.bInOpenSection = false;
   gd.hopoRules = rules;
   gd.fretType = fretType;
   
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
   mo.FretType = FIVE_FRETS;
   mo.midFile = mf;
   mo.bassTrack = NULL;
   mo.guitarTrack = NULL;
   mo.drumTrack = NULL;
   mo.vocalTrack = NULL;
   mo.eventTrack = NULL;
   mo.beatTrack = NULL;
   mo.venueTrack = NULL;
   mo.otherTrack = NULL;
   
   // technically not kosher, but lets make it work anyway
   // some custom midis are just one track, beat and guitar rolled into one
   if( mf->numTracks == 1 )
   {
      mo.guitarTrack = mf->tracks[0];
      mo.beatTrack = mf->tracks[0];
      mo.HOPOType = GH_HOPO_Rules;
      return mo;
   }
   
   for(int i = mf->numTracks - 1; i >= 0; i--)
   {
      MidiFile::MidiEvent* tempTrk = mf->tracks[i];
      if(tempTrk->type == MidiFile::MidiEventType_Meta)
      {
         // skip ahead of any extra events
         while( tempTrk->subType != MidiFile::MidiMeta_TrackName &&
               tempTrk->subType != MidiFile::MidiMeta_Tempo &&
               tempTrk->subType != MidiFile::MidiMeta_TimeSignature)
         {
            tempTrk = tempTrk->pNext;
         }
         
         if(tempTrk->subType == MidiFile::MidiMeta_TrackName)
         {
            // found one of the important tracks, figure out which it is
            MidiFile::MidiEvent_Text* tempTxt = (MidiFile::MidiEvent_Text*) tempTrk;
            if(compareToString(tempTxt->buffer, "PART GUITAR") ||
               compareToString(tempTxt->buffer, "PART_GUITAR") ||
               compareToString(tempTxt->buffer, "T1 GEMS")) {
               mo.guitarTrack = tempTrk;
            } else if(compareToString(tempTxt->buffer, "PART GUITAR GHL")) {
               mo.guitarTrack = tempTrk;
               mo.FretType = SIX_FRETS;
            } else if(compareToString(tempTxt->buffer, "PART BASS") ||
                      compareToString(tempTxt->buffer, "PART_BASS") ||
                      compareToString(tempTxt->buffer, "PART RHYTHM") ||
                      compareToString(tempTxt->buffer, "PART_RHYTHM")) {
               mo.bassTrack = tempTrk;
            } else if(compareToString(tempTxt->buffer, "PART BASS GHL")) {
               mo.bassTrack = tempTrk;
               mo.FretType = SIX_FRETS;
            }
            // Drums and vocals means this is a RB song
            // AFAIK anything past GH3 never had midis ripped because people thought
            // the new features would screw with existing midi parsers
            // EDIT: I was wrong, people make midis with new features too
            else if(compareToString(tempTxt->buffer, "PART DRUMS") ||
                    compareToString(tempTxt->buffer, "PART_DRUMS") ||
                    compareToString(tempTxt->buffer, "BAND DRUMS") ||
                    compareToString(tempTxt->buffer, "BAND_DRUMS")) {
               mo.drumTrack = tempTrk;
               if(mo.HOPOType == Unknown_Rules) mo.HOPOType = RB_HOPO_Rules;
            } else if(compareToString(tempTxt->buffer, "PART VOCALS") ||
                      compareToString(tempTxt->buffer, "PART_VOCALS") ||
                      compareToString(tempTxt->buffer, "BAND SINGER") ||
                      compareToString(tempTxt->buffer, "BAND_SINGER")) {
               mo.vocalTrack = tempTrk;
               if(mo.HOPOType == Unknown_Rules) mo.HOPOType = RB_HOPO_Rules;
            }
            // These will probably never be used, but save them just in case
            else if(compareToString(tempTxt->buffer, "EVENTS")) {
               mo.eventTrack = tempTrk;
            }  else if(compareToString(tempTxt->buffer, "VENUE")) {
               mo.venueTrack = tempTrk;
            }  else if(compareToString(tempTxt->buffer, "BEAT")) {
               mo.beatTrack = tempTrk;
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

// TODO: find ranges for GHL midis and put them in here
void getNoteRangeForDifficulty(Difficulty diff, ChartFrets fretType, int* lower, int* upper)
{
   bool isGHL = false;
   if (fretType == SIX_FRETS) isGHL = true;
   
   switch(diff)
   {
      case Difficulty_Easy:
         *lower = (isGHL ? 58 : 60);
         *upper = 66;
         break;
      case Difficulty_Medium:
         *lower = (isGHL ? 70 : 72);
         *upper = 78;
         break;
      case Difficulty_Hard:
         *lower = (isGHL ? 82 : 84);
         *upper = 90;
         break;
      case Difficulty_Challenge:
         *lower = (isGHL ? 94 : 96);
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
   // If forced, follow those rules
   if( gd.iLastForcedStrum == iNoteMark )
   {
      return false;
   }
   if( gd.iLastForcedHOPO == iNoteMark && gd.iLastForcedHOPO != -1 )
   {
      return true;
   }
   // if this note is in a chord, then HOPO=no
   if( gd.iLastChordRow == iNoteMark && gd.iLastChordRow != -1 )
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
   
   return ShouldBeHOPO;
}

// Adds a note to the note data based on Guitar Hero/RockBand rules
void addGHRBNote(NoteData &notes, int col, int start, int end, GuitarData &gd)
{
   bool isChordRow = false;
   //TapNote taps[gd.iCols]; this doesn't work in some compilers
   TapNote *taps = new TapNote[gd.iCols];
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
   // reorganize the notes for guitar hero live, since GHL midis put open strum as col 0
   if(gd.iCols == 7 && col < 7)
   {
      col--;
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
   
   // determine if this is a forced note (row 5 for 5fret, 7 in 6fret)
   if((col == 5 && gd.iCols == 6) || (col == 7 && gd.iCols == 7))
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
               placeNote(notes, i, startRow, startRow + taps[i].iDuration, 3);
            }
         }
      }
      // mark last forced note row
      gd.iLastForcedHOPO = start;
   }
   // this row means not a hopo
   else if((col == 6 && gd.iCols == 6) || (col == 8 && gd.iCols == 7))
   {
      // check if any other notes were placed on this row
      if(isChordRow)
      {
         // replace any notes on this row with taps/holds
         for(int i=0; i<gd.iCols; i++)
         {
            if(taps[i] == TAP_EMPTY) continue;
            placeNote(notes, i, startRow, startRow + taps[i].iDuration, 2);
         }
      }
      // mark last normal note row
      gd.iLastForcedStrum = start;
   }
   else // this is a normal note
   {
      // move open strum to the appropriate column
      if(gd.bInOpenSection || (col == -1 && gd.iCols == 7)) col = gd.iCols - 1;
      
      // place a tap/hold if this row is a tap row
      if(gd.bInTapSection)
      {
         placeNote(notes, col, startRow, endRow, 1);
      }
      else
      {
         // if this row forms a chord
         if(isChordRow)
         {
            // get if the last note was a hopo, should only be relevant for 1 note in row so this'll work
            bool wasHopo = taps[highestNote].type == TapNoteType_HOPO || taps[highestNote].type == TapNoteType_HOPOHold;
            // check if this row was forced
            if(start == gd.iLastForcedHOPO)
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
   
   // clean up temporary array
   delete[](taps);
}

/**
 * Gets the notes from a midi track based on the standard rules of Guitar Hero/Rock Band
 * cols parameter is just in case drums or six-fret can be loaded in the future
 */
NoteData getGHRBNotesFromTrack(MidiFile::MidiEvent* track, Difficulty diff, HOPORules rules, ChartFrets fretType,
                               int resolution, int HOPOres)
{
   NoteData newNotes;
   MidiFile::MidiEvent* curEvt = track;
   // note bounds for difficulty
   int low = 0;
   int high = 0;
   int cols = 0;
   // 5fret: 6 columns in note data, 7 midi columns
   // 6fret: 7 columns in note data, 9 midi columns
   // why? 6fret has an extra col in midi for open notes, 5fret doesn't
   if (fretType == FIVE_FRETS) cols = 6;
   else if(fretType == SIX_FRETS) cols = 7;
   int bonusCols = 1;
   if (fretType == SIX_FRETS) bonusCols = 2;
   
   std::vector<MidiFile::MidiEvent_Note*> notesInProgress(cols+bonusCols);
   
   getNoteRangeForDifficulty(diff, fretType, &low, &high);
   newNotes.SetNumTracks(cols);
   for(int i=0; i<cols+bonusCols; i++)
   {
      notesInProgress[i] = NULL;
   }
   
   GuitarData gd = getNewGuitarData(resolution, HOPOres, rules, fretType);
   
   // While there are more events
   while(curEvt)
   {
      // Sysex events denote special things like taps and open notes
      if(curEvt->type == MidiFile::MidiEventType_SYSEX)
      {
         MidiFile::MidiEvent_SYSEX* tempSysex = (MidiFile::MidiEvent_SYSEX*) curEvt;
         int type = translateSysex(tempSysex->pData, diff);
         if (type > 2)
         {
            gd.bInTapSection = (type % 2 == 1);
         }
         else if (type > 0)
         {
            gd.bInOpenSection = (type % 2 == 1);
         }
      }
      else if(curEvt->type == MidiFile::MidiEventType_Note)
      {
         MidiFile::MidiEvent_Note* tempNote = (MidiFile::MidiEvent_Note*) curEvt;
         int idx = tempNote->note - low;
         // Is this note in the range we care about?
         if(idx >= 0 && tempNote->note <= high)
         {
            // because guitar hero is special, note on events with velocity 0 = note off
            if(tempNote->subType == MidiFile::MidiNote_NoteOn &&
               tempNote->velocity > 0)
            {
               if(notesInProgress[idx] == NULL)
                  notesInProgress[idx] = tempNote;
            }
            else if(tempNote->subType == MidiFile::MidiNote_NoteOff ||
                    (tempNote->subType == MidiFile::MidiNote_NoteOn &&
                     tempNote->velocity == 0))
            {
               // close off any note in progress and place either a hold or tap note
               if(notesInProgress[idx])
               {
                  int start = notesInProgress[idx]->tick;
                  int end = tempNote->tick;
                  
                  // quick sanity check...
                  if(end > start)
                  {
                     // add the note
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
         int idx = tempNote->note % cols;
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
      *hopoResolution = (*resolution / 3) + 10;
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
               *hopoResolution = (*resolution / 6) + 10; // = 90 (1/16 note thirds)
               break;
            case 1:
               // few HOPOs
               *hopoResolution = (*resolution / 4) + 10;// = 130 (1/16th notes)
               break;
            case 2:
            default:
               // normal HOPOs
               *hopoResolution = (*resolution / 3) + 10; // = 170 standard (1/8th note 3rds)
               break;
            case 3:
               // more HOPOs
               *hopoResolution = (*resolution / 2) + 10;// = 250 (1/8th notes)
               break;
            case 4:
               // most HOPOs
               *hopoResolution = (*resolution / 1.5) + 10; // = 330 (1/2 note 3rds)
               break;
            case 5:
               // most HOPOs
               *hopoResolution = *resolution + 10; // = 490 (1/4 notes)
               break;
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
         if(!songFiles[i].compare("guitar.ogg"))
         {
            out.m_sInstrumentTrackFile[InstrumentTrack_Guitar] = path + songFiles[i];
         } else if(!songFiles[i].compare("song.ogg"))
         {
            out.m_sMusicFile = path + songFiles[i];
         } else if(!songFiles[i].compare("rhythm.ogg") ||
                   !songFiles[i].compare("bass.ogg"))
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
   int lastLyric = 0;
   std::string curLine = "";
   bool phraseOpen = false;
   
   if( !f.Open(lrcFileName, RageFile::WRITE) )
   {
      LOG->UserLog( "Lyrics file at", path, "couldn't be opened for writing: %s", f.GetError().c_str() );
      return "";
   }
   
   // format for .lrc files:
   // [mm:ss.xx]Lyrics here
   // [mm:ss.xx]More lyrics
   // use the divisions used in RB on notes 105 and 106
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
               
               // '-' is used to connect syllables inside words
               if(curLine.back() == '-') {
                  curLine = curLine.substr(0, curLine.size() - 1);
               } else {
                  curLine += ' ';
               }
               
               // append to line
               curLine += txt;
               lastLyric = txtEvent->tick;
            }
         }
      }
      else if (curEvt->type == MidiFile::MidiEventType_Note)
      {
         MidiFile::MidiEvent_Note* tempNote = (MidiFile::MidiEvent_Note*) curEvt;
         if (tempNote->note == 105 || tempNote->note == 106)
         {
            // beginning of a new phrase, format the line
            if (tempNote->subType == MidiFile::MidiNote_NoteOn && tempNote->velocity > 0 && !phraseOpen)
            {
               // if more than a beat elapsed from last lyrics, insert a blank line
               if (tempNote->tick - lastMeasure > resolution)
               {
                  std::string blankLine = getTimeString(td.GetElapsedTimeFromBeat( (float)lastMeasure / resolution ));
                  f.PutLine(blankLine);
               }
               curLine = getTimeString(td.GetElapsedTimeFromBeat( (float)tempNote->tick / resolution ));
               phraseOpen = true;
            }
            // end of a phrase, append to file
            else if ((tempNote->subType == MidiFile::MidiNote_NoteOff ||
                      (tempNote->subType == MidiFile::MidiNote_NoteOn && tempNote->velocity == 0)) && phraseOpen)
            {
               f.PutLine(curLine);
               lastMeasure = tempNote->tick;
               // if more than a measure elapsed since the last lyric, put a blank line in too
               if( lastMeasure - lastLyric >= resolution * 4 )
               {
                  std::string blankLine = getTimeString(td.GetElapsedTimeFromBeat( (float)(lastLyric +
                                                                                 (4 * resolution)) / resolution ));
                  f.PutLine(blankLine);
               }
               phraseOpen = false;
            }
         }
      }
      
      curEvt = curEvt->pNext;
   }
   
   f.PutLine(curLine);
   if( f.Flush() == -1 )
      return "";
   
   f.Close();
   return lrcFileName;
}

void MIDILoader::GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out )
{
   GetDirListing( sPath + std::string("*.mid"), out );
}

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
   int hopoResolution = (resolution / 3) + 10;
   std::string title = "";
   std::string artist = "";
   std::string charter = "";
   parseINI(dir, &resolution, &hopoResolution, title, artist, charter);
   
   out.m_sMainTitle = title;
   out.m_sArtist = artist;
   out.m_sCredit = charter;
   out.m_sSongFileName = dir+arrayMidiFileNames[0];
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
      if( lyricFiles.size() != 0 )
      {
         lrcFile = sDir + lyricFiles[0];
      }
   }
   if(!lrcFile.empty()) out.m_sLyricsFile = lrcFile;
   
   // Get all notes from the guitar track
   for(int i=0; i<8; i++)
   {
      // Skip if there's no bass/rhythm track
      if( i >= 4 && mo.bassTrack == NULL ) break;
      Steps* newSteps = out.CreateSteps();
      /* steps initialization stuff */
      if(mo.FretType == FIVE_FRETS)
         newSteps->m_StepsType = (i < 4 ? StepsType_guitar_solo : StepsType_guitar_backup);
      else if(mo.FretType == SIX_FRETS)
         newSteps->m_StepsType = (i < 4 ? StepsType_guitar_solo6 : StepsType_guitar_backup6);
      newSteps->SetChartStyle("Guitar");
      newSteps->SetCredit( charter );
      newSteps->SetDescription( charter );
      // out.SetMusicFile( headerInfo[2] ); // TODO: music stuff needed?
      newSteps->SetMeter(1); // will have to try this later, has a history of crashing when not hardcoded
      //newSteps->SetSavedToDisk(true);
       
      newSteps->SetFilename(dir+arrayMidiFileNames[0]);
      switch(i) {
         case 0:
            newSteps->SetDifficulty(Difficulty_Easy);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Easy, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 1:
            newSteps->SetDifficulty(Difficulty_Medium);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Medium, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 2:
            newSteps->SetDifficulty(Difficulty_Hard);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Hard, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 3:
            newSteps->SetDifficulty(Difficulty_Challenge);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, Difficulty_Challenge, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 4:
            newSteps->SetDifficulty(Difficulty_Easy);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.bassTrack, Difficulty_Easy, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 5:
            newSteps->SetDifficulty(Difficulty_Medium);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.bassTrack, Difficulty_Medium, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 6:
            newSteps->SetDifficulty(Difficulty_Hard);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.bassTrack, Difficulty_Hard, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
            break;
         case 7:
            newSteps->SetDifficulty(Difficulty_Challenge);
            newSteps->SetNoteData(getGHRBNotesFromTrack(mo.bassTrack, Difficulty_Challenge, mo.HOPOType,
                                                        mo.FretType, resolution, hopoResolution));
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

bool MIDILoader::LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out ) {
   // Parse the midi file
   MidiFile *mf = ReadMidiFile(cachePath);
   
   // Organize the midi
   MidiOrganizer mo = organizeMidi(mf);
   
   // Get the HOPO resolution
   std::string sBasePath = cachePath.substr(0, cachePath.find_last_of("/")+1);
   int resolution = mf->ticksPerBeat;
   int hopoResolution = (resolution / 3) + 10;
   std::string title = "";
   std::string artist = "";
   std::string charter = "";
   parseINI(sBasePath, &resolution, &hopoResolution, title, artist, charter);
   
   // Get the desired notes from the guitar track
   if( out.m_StepsType == StepsType_guitar_backup || out.m_StepsType == StepsType_guitar_backup6 )
      out.SetNoteData(getGHRBNotesFromTrack(mo.bassTrack, out.GetDifficulty(), mo.HOPOType,
                                         mo.FretType, resolution, hopoResolution));
   else
      out.SetNoteData(getGHRBNotesFromTrack(mo.guitarTrack, out.GetDifficulty(), mo.HOPOType,
                                            mo.FretType, resolution, hopoResolution));
   out.TidyUpData();
   
   return true;
}
