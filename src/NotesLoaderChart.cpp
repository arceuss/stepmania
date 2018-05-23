#include "global.h"
#include "NotesLoaderCHART.h"
#include "RageUtil_CharConversions.h"
#include "RageFile.h"
#include "TimingData.h"
#include "RageUtil.h"
#include "RageLog.h"
#include "BackgroundUtil.h"
#include "NoteData.h"
#include "Song.h"
#include "Steps.h"
#include "GameManager.h"
#include "Difficulty.h"
#include "IniFile.h"
#include <sstream>

/*
 This may not not look pretty, but this is pretty faithfully accurate for this weird file format
 */

// extracted method to save lines of code and readability
// noteCategory (selected based on duration)
//    1 - tap/hold
//    2 - gem/gemhold
//    3 - hopo/hopohold
void addNote(NoteData &notes, int track, int start, int end, int noteCategory)
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
      // invalid type
      return;
   }
   
   if( end > start )
   {
      notes.AddHoldNote(track, start, end, heldTapKind);
   } else {
      notes.SetTapNote(track, start, singleTapKind);
   }
}

std::vector<std::string> getLineWords(std::istringstream &iss)
{
   std::string line;
   std::getline(iss,line);
   // Remove tabs
   line.erase(std::remove(line.begin(), line.end(), '\t'), line.end());
   std::vector<std::string> vsWords;
   std::istringstream lineIss(line);
   // Separate by spaces
   copy( std::istream_iterator<std::string>(lineIss), std::istream_iterator<std::string>(), back_inserter(vsWords) );
   return vsWords;
}

NoteData parseNoteSection(std::istringstream &iss, int resolution, int iHopoResolution, bool isGHL)
{
   int numCols = isGHL ? 7 : 6;
   NoteData newNotes;
   bool keepReading = true;
   
   // setup
   newNotes.SetNumTracks(numCols);
   // vectors so columns can be checked individually
   std::vector<int> iPrevNoteMark(numCols);
   int iPrevNoteTrack = -1;
   std::vector<int> iPrevNoteLength(numCols);
   std::vector<bool> bPrevNoteHOPO(numCols);
   for(int i=0; i<(numCols); i++) {
      iPrevNoteMark[i] = -1;
      iPrevNoteLength[i] = -1;
      bPrevNoteHOPO[i] = false;
   }
   
   // For keeping track of forced note markers, denoted by "E *" or "N 5"
   int iLastForcedRow = -1;
   // For keeping track of forced tap notes, denoted by "E T"
   int iLastTapRow = -1;
   // HOPOs should never immediately follow chords because reasons
   int iLastChordRow = -1;
   
   // don't do this here?
   //pNewNotes->SetDifficulty( Difficulty_Challenge );
   //pNewNotes->SetChartName( headerInfo[0]+" - Expert" );
   
   while( keepReading )
   {
      std::vector<std::string> vsWords = getLineWords(iss);
      
      // check special cases
      if(vsWords.empty()) break; // end of file
      if(vsWords[0].at(0) == '{') continue; // random open brace at start, skip it
      if(vsWords[0].at(0) == '}') break; // end brace found, we're done here
      
      // ACTUAL NOTE PARSING HERE
      // TODO: further extract methods to reduce number of lines
         
      // Parse the notedata
      // Parsing special cases first
      if( !vsWords[2].compare( "E" ) )
      {
         // This denotes a forced note, this is usually because a charter forgot to change this to "N 5"
         if( !vsWords[3].compare( "*" ) )
         {
            iLastForcedRow = atoi(vsWords[0].c_str());
            
            // Search back and change any notes on this row to their opposite
            TapNote tn = TAP_EMPTY;
            
            for( int l=0; l<numCols - 1; l++ ) {
               tn = newNotes.GetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution));
               if( tn != TAP_EMPTY ) {
                  bool wasHopo = tn.type == TapNoteType_HOPO || tn.type == TapNoteType_HOPOHold;
                  addNote(newNotes, l, BeatToNoteRow((float)iLastForcedRow/resolution),
                          BeatToNoteRow((float)iLastForcedRow/resolution) + tn.iDuration, wasHopo ? 2 : 3);
               }
            }
         }
         
         // This denotes a tap note, or slider note, it's literally a tap note in Stepmania
         if( !vsWords[3].compare( "T" ) )
         {
            iLastTapRow = atoi(vsWords[0].c_str());
            
            // Search back and change any notes on this row to tap notes
            TapNote tn = TAP_EMPTY;
            
            for( int l=0; l<numCols - 1; l++ ) {
               tn = newNotes.GetTapNote(l, BeatToNoteRow((float)iLastTapRow/resolution));
               
               if( tn != TAP_EMPTY ) {
                  addNote(newNotes, l, BeatToNoteRow((float)iLastTapRow/resolution),
                          BeatToNoteRow((float)iLastTapRow/resolution) + tn.iDuration, 1);
               }
            }
         }
      }
      
      if( !vsWords[2].compare( "N" ) )
      {
         int iNoteTrack = atoi(vsWords[3].c_str());
         
         /* Track 5 is used to denote forced notes (sometimes E * is), this means that any note at the same
          * time as that are toggled between HOPO and strum notes. It is track 8 for GHL
          */
         if( (iNoteTrack == 5 && !isGHL) || (iNoteTrack == 8 && isGHL) )
         {
            // copy/paste is bad, extract to new function?
            iLastForcedRow = atoi(vsWords[0].c_str());
            
            // Search back and change any notes on this row to their opposite
            TapNote tn = TAP_EMPTY;
            
            for( int l=0; l<numCols - 1; l++ ) {
               tn = newNotes.GetTapNote(l, BeatToNoteRow((float)iLastForcedRow/resolution));
               if( tn != TAP_EMPTY ) {
                  bool wasHopo = tn.type == TapNoteType_HOPO || tn.type == TapNoteType_HOPOHold;
                  addNote(newNotes, l, BeatToNoteRow((float)iLastForcedRow/resolution),
                          BeatToNoteRow((float)iLastForcedRow/resolution) + tn.iDuration, wasHopo ? 2 : 3);
               }
            }
            
            continue;
         }
         
         /* Yet another denotation for tap notes
          * do the same thing as above.
          * Yes there's a lot of copy/paste for ass-backwards compatibility
          */
         if( iNoteTrack == 6 )
         {
            iLastTapRow = atoi(vsWords[0].c_str());
            
            // Search back and change any notes on this row to tap notes
            TapNote tn = TAP_EMPTY;
            
            for( int l=0; l<numCols - 1; l++ ) {
               tn = newNotes.GetTapNote(l, BeatToNoteRow((float)iLastTapRow/resolution));
               
               if( tn != TAP_EMPTY ) {
                  addNote(newNotes, l, BeatToNoteRow((float)iLastTapRow/resolution),
                          BeatToNoteRow((float)iLastTapRow/resolution) + tn.iDuration, 1);
               }
            }
            
            continue;
         }
         
         // This is open strums, so just change it to that
         if( iNoteTrack == 7 )
            iNoteTrack = isGHL ? 6 : 5;
         
         /* A note on sustained notes, Guitar Hero likes to have holds end exactly on beats, unfortunately this means that
          * sometimes a hold note can overlap into the next hold if they're in the same track. Need to check for this and
          * correct it by shortening the first hold slightly (32nd note shorter than full beat)
          * Also, rounding errors in Chart2Mid2Chart mean it can be off by 1
          * NOTE: in midi, I skip all this by just shortening all holds by 1/32, so lets make this even
          */
         int iNoteMark = atoi(vsWords[0].c_str());
         int iNoteLength = atoi(vsWords[4].c_str()) - (resolution / 8);
         /*
         if( iPrevNoteLength[iNoteTrack] + iPrevNoteMark[iNoteTrack] + 1 >= iNoteMark ) {
            // sustain note correction
            newNotes.SetTapNote(iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution), TAP_EMPTY);
            iPrevNoteLength[iNoteTrack] = iNoteMark - iPrevNoteMark[iNoteTrack] - (resolution / 8);
            
            addNote(newNotes, iNoteTrack, BeatToNoteRow((float)iPrevNoteMark[iNoteTrack]/resolution),
                    BeatToNoteRow((float)(iPrevNoteMark[iNoteTrack] + iPrevNoteLength[iNoteTrack])/resolution),
                    bPrevNoteHOPO[iNoteTrack] ? 3 : 2);
         }*/
         
         // If this note and the previous note are on the same beat and the previous note was a HOPO,
         // Change it to a normal gem
         for( int k=0; k<numCols - 1; ++k )
         {
            if( bPrevNoteHOPO[k] && k != iNoteTrack && std::abs(iNoteMark - iPrevNoteMark[k]) <= 1 )
            {
               iLastChordRow = iNoteMark;
               addNote(newNotes, k, BeatToNoteRow((float)iPrevNoteMark[k]/resolution),
                       BeatToNoteRow((float)(iPrevNoteMark[k] + iPrevNoteLength[k])/resolution), 2);
               bPrevNoteHOPO[k] = false;
            }
         }
         
         // Specially marked tap notes override all HOPO rules
         if( std::abs(iLastTapRow - iNoteMark) <= 1 && iLastTapRow != -1 )
         {
            addNote(newNotes, iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution),
                    BeatToNoteRow((float)(iNoteMark + iNoteLength)/resolution), 1);
            bPrevNoteHOPO[iNoteTrack] = false;
         }
         else
         {
            
            /* If the difference between this note and the last is <= a 16th note (and > 0), and they're
             * on different tracks, AND this isn't the first note, then make this a HOPO
             * If the row is forced, strum notes become HOPOs, and HOPOs become strum notes
             * And as an extra rule, if this note is following a chord and the note was part of the chord,
             * then it cannot be a HOPO
             */
            bool ShouldBeHOPO = false;
            
            for( int k=0; k<numCols - 1; ++k )
            {
               // difference is less than hopo resolution, notes on different tracks, and it's not the 1st note, HOPO=yes
               if((std::abs(iNoteMark - iPrevNoteMark[k]) - 1 <= iHopoResolution) && (iNoteTrack != iPrevNoteTrack) &&
                  (iPrevNoteMark[k] != -1)) ShouldBeHOPO = true;
               
               // if this note is in a chord, then HOPO=no and that's that
               if( (std::abs(iNoteMark - iPrevNoteMark[k]) <= 1) || (std::abs(iLastChordRow - iNoteMark) <= 1 &&
                                                                     iLastChordRow != -1) )
               {
                  ShouldBeHOPO = false;
                  break;
               }
               
               // if this is following a chord of which this row was a part of, it is definitely not a HOPO
               if( iPrevNoteTrack != -1 && iPrevNoteMark[iNoteTrack] != -1 && iPrevNoteMark[iPrevNoteTrack] != -1 &&
                  std::abs(iPrevNoteMark[iNoteTrack] - iPrevNoteMark[iPrevNoteTrack]) <= 1 &&
                  iPrevNoteTrack != iNoteTrack)
               {
                  ShouldBeHOPO = false;
                  break;
               }
            }
            
            // Reverse the note if the row was marked to be forced
            if( std::abs(iLastForcedRow - iNoteMark) <= 1 && iLastForcedRow != -1 ) ShouldBeHOPO = !ShouldBeHOPO;
            
            // plop in either a gem or HOPO
            addNote(newNotes, iNoteTrack, BeatToNoteRow((float)iNoteMark/resolution),
                    BeatToNoteRow((float)(iNoteMark + iNoteLength)/resolution), ShouldBeHOPO ? 3 : 2);
            bPrevNoteHOPO[iNoteTrack] = ShouldBeHOPO;
         }
         
         iPrevNoteMark[iNoteTrack] = iNoteMark;
         iPrevNoteTrack = iNoteTrack;
         iPrevNoteLength[iNoteTrack] = iNoteLength;
      }
      // end all note parsing
   }
   
   return newNotes;
}

// TODO: better way of passing all these parameters? some helper data storage class?

std::vector<std::string> parseHeader(std::istringstream &iss, Song &outSong, int* resolution, int* iHopoResolution,
                                     std::string sFilePath, bool parseSongInfo)
{
   bool keepReading = true;
   std::vector<std::string> headerInfo(3);
   bool onlyOneSong = true;
   
   while(keepReading)
   {
      std::vector<std::string> vsWords = getLineWords(iss);
      
      // special cases
      if(vsWords.empty()) break; // end of file
      if(vsWords[0].at(0) == '{') continue; // ignore random open brace
      if(vsWords[0].at(0) == '}') break; // end brace found, we're done here
      
      // Parse song information
      // Separation by words also means that titles, artists and other info is likely broken up, stitch it back together
      
      int j = 2;
      std::string wholeString = "";
      while(j < vsWords.size()) {
         if(j>2) wholeString += " ";
         wholeString += vsWords[j];
         j++;
      }
      
      if( !vsWords[0].compare("Name") )
      {
         std::string mainTitle = wholeString.substr(1, wholeString.size()-2);
         headerInfo[0] = mainTitle;
         if(parseSongInfo) outSong.m_sMainTitle = mainTitle;
      } else if( !vsWords[0].compare("Artist") && parseSongInfo )
      {
         outSong.m_sArtist = wholeString.substr(1, wholeString.size()-2);
      } else if( !vsWords[0].compare("Charter") )
      {
         headerInfo[1] = wholeString.substr(1, wholeString.size()-2);
      } else if( !vsWords[0].compare("Offset") && parseSongInfo )
      {
         outSong.m_SongTiming.set_offset(-1 * atof(vsWords[2].c_str()));
         
         // Dunno if this will actually do anything, but it seems to be defaults put into SSC files for some reason?
         outSong.m_SongTiming.AddSegment(ComboSegment(0.0,1,1));
         outSong.m_SongTiming.AddSegment(ScrollSegment(0.0,1.0));
      } else if( !vsWords[0].compare("Resolution") )
      {
         /* Here's the thing, bad charts have a default resolution of 192 ticks per beat. Comparatively, Stepmania uses
          * 192 ticks per MEASURE as a maximum (real Guitar Hero and Rock Band charts have a resolution of 480 per beat)
          * This causes a lot of problems. If the resolution is >48, just make it 48
          * But only for setting the tickcount segment, still need the original resolution for note and timing parsing
          * Also, parse the ini here since it's only useful for HOPO resolutions
          * actually the ini may have other useful data in it, but that's for later
          */
         *resolution = atoi(vsWords[2].c_str());
         
         IniFile ini;
         if( !ini.ReadFile( sFilePath + "song.ini" ) )
         {
            // could not find ini file, oh well
            *iHopoResolution = (*resolution / 3) + 4;
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
                     *iHopoResolution = (*resolution / 6) + 4; // = 90 (1/16 note thirds)
                     break;
                  case 1:
                     // few HOPOs
                     *iHopoResolution = (*resolution / 4) + 4;// = 130 (1/16th notes)
                     break;
                  case 2:
                  default:
                     // normal HOPOs
                     *iHopoResolution = (*resolution / 3) + 4; // = 170 standard (1/8th note 3rds)
                     break;
                  case 3:
                     // more HOPOs
                     *iHopoResolution = (*resolution / 2) + 4;// = 250 (1/8th notes)
                     break;
                  case 4:
                     // most HOPOs
                     *iHopoResolution = (*resolution / 1.5) + 4; // = 330 (1/2 note 3rds)
                     break;
                  case 5:
                     // most HOPOs
                     *iHopoResolution = *resolution + 4; // = 490 (1/4 notes)
                     break;
               }
            }
            // next, if eighthnotes count as hopos, divide the hopoResolution in half
            if( ini.GetValue("song", "eighthnote_hopo", eightNoteHopo) ) {
               if( eightNoteHopo ) *iHopoResolution /= 2;
            }
         }
         
         if( parseSongInfo ) {
            if( *resolution <= 48 ) outSong.m_SongTiming.AddSegment(TickcountSegment(0, *resolution));
            else outSong.m_SongTiming.AddSegment(TickcountSegment(0, 48));
         }
      } else if( !vsWords[0].compare("Difficulty") )
      {
         /* Uh, skip this for now, causes Stepmania to crash...
          if( parseSongInfo ) pNewNotes->SetMeter(atoi(vsWords[2].c_str()) + 1);
          else outSteps->SetMeter(atoi(vsWords[2].c_str()) + 1);
          */
      } else if( !vsWords[0].compare("PreviewStart") && parseSongInfo )
      {
         outSong.m_fMusicSampleStartSeconds = atof(vsWords[2].c_str());
      } else if( !vsWords[0].compare("PreviewEnd") && parseSongInfo )
      {
         // Default to 12 second length, just seems like the default in StepMania
         float estSampleSec = atof(vsWords[2].c_str()) - outSong.m_fMusicSampleStartSeconds;
         if( estSampleSec <= 12 ) outSong.m_fMusicSampleStartSeconds = 12;
         else outSong.m_fMusicSampleLengthSeconds = estSampleSec;
      } else if( !vsWords[0].compare("MusicStream") )
      {
         std::string songFile = sFilePath + wholeString.substr(1, wholeString.size()-2);
         if( parseSongInfo ) outSong.m_sMusicFile = songFile;
         headerInfo[2] = songFile;
      } else if( !vsWords[0].compare("GuitarStream") )
      {
         onlyOneSong = false;
         std::string songFile = sFilePath + wholeString.substr(1, wholeString.size()-2);
         if( parseSongInfo ) outSong.m_sInstrumentTrackFile[InstrumentTrack_Guitar] = songFile;
      } else if( !vsWords[0].compare("BassStream") )
      {
         onlyOneSong = false;
         std::string songFile = sFilePath + wholeString.substr(1, wholeString.size()-2);
         if( parseSongInfo ) outSong.m_sInstrumentTrackFile[InstrumentTrack_Bass] = songFile;
      }
   }
   
   if(onlyOneSong && parseSongInfo)
   {
      outSong.m_sInstrumentTrackFile[InstrumentTrack_Rhythm] = "";
   }
   
   return headerInfo;
}

void parseSyncTrack(std::istringstream &iss, TimingData &out, int resolution)
{
   bool keepReading = true;
   
   while( keepReading )
   {
      std::vector<std::string> vsWords = getLineWords(iss);
      
      // check special cases
      if(vsWords.empty()) break; // end of file
      if(vsWords[0].at(0) == '{') continue; // random open brace at start, skip it
      if(vsWords[0].at(0) == '}') break; // end brace found, we're done here
      
      // Parse BPM and time signature changes
      int startMark = atoi(vsWords[0].c_str());
      float newBPM = atof(vsWords[3].c_str()) / 1000;
      
      if( !vsWords[2].compare( "B" ) )
      {
         out.AddSegment( BPMSegment(BeatToNoteRow((float)startMark/resolution), newBPM ));
      } else if( !vsWords[2].compare( "TS" ) )
      {
         // Fun fact, Guitar Hero songs are always in a time signature of x/4
         out.AddSegment( TimeSignatureSegment(BeatToNoteRow(atof(vsWords[0].c_str()) / resolution),
                                                           atoi(vsWords[3].c_str()), 4) );
      }
   }
}

void parseEvents(std::istringstream &iss, Song &out, int resolution)
{
   bool keepReading = true;
   
   while( keepReading )
   {
      std::vector<std::string> vsWords = getLineWords(iss);
      
      // check special cases
      if(vsWords.empty()) break; // end of file
      if(vsWords[0].at(0) == '{') continue; // random open brace at start, skip it
      if(vsWords[0].at(0) == '}') break; // end brace found, we're done here
      
      // Parse section labels
      // on good charts, this will only be the section title
      // on bad charts, this will include a bunch of garbage too
      // sometimes this even has spaces, sometimes it doesn't, people can't make up their damn minds
      int end = vsWords.size()-1;
      std::string sectionTitle = "";
      int i = 4;
      while( i < end )
      {
         sectionTitle += vsWords[i];
         sectionTitle += ' ';
         i++;
      }
      
      sectionTitle += vsWords[end].substr(0, vsWords[end].size()-1);
      // some charts have erroneous sections and that makes this annoying
      if(sectionTitle.size() > 2) {
         out.m_SongTiming.AddSegment(LabelSegment(BeatToNoteRow(atof(vsWords[0].c_str())/resolution),sectionTitle));
      }
   }
}

std::string difficultyToString(Difficulty diff)
{
   switch(diff) {
      case Difficulty_Challenge:
         return "Expert";
         break;
      case Difficulty_Hard:
         return "Hard";
         break;
      case Difficulty_Medium:
         return "Medium";
         break;
      case Difficulty_Easy:
         return "Easy";
         break;
      default:
         return "Unknown";
   }
}

Difficulty parseDifficulty(std::string str)
{
   if( str.find("Expert") != std::string::npos ) {
      return Difficulty_Challenge;
   } else if( str.find("Hard") != std::string::npos ) {
      return Difficulty_Hard;
   } else if( str.find("Medium") != std::string::npos ) {
      return Difficulty_Medium;
   } else if( str.find("Easy") != std::string::npos ) {
      return Difficulty_Easy;
   } else {
      return Difficulty_Invalid;
   }
}

void initSteps(Steps &out, Difficulty diff, std::istringstream &iss, int resolution, int hopoResolution,
               std::vector<std::string> headerInfo, bool isGHL, bool isBackup)
{
   // some of this is default BS, only weirdos with guitars use .chart files
   if( isBackup )
      out.m_StepsType = isGHL ? StepsType_guitar_backup6 : StepsType_guitar_backup;
   else
      out.m_StepsType = isGHL ? StepsType_guitar_solo6 : StepsType_guitar_solo;
   out.SetChartStyle("Guitar");
   out.SetCredit( headerInfo[1] );
   out.SetDescription( headerInfo[1] );
   out.SetMusicFile( headerInfo[2] );
   out.SetDifficulty(diff);
   out.SetMeter(1); // yeah there's a way to get this from the chart, will try later
   out.SetNoteData(parseNoteSection(iss, resolution, hopoResolution, isGHL));
   out.TidyUpData();
   out.SetSavedToDisk(true);
}

bool ReadBuf( const char *buf, int len, Song &outSong, Steps &outSteps, bool parseSongInfo, std::string sFilePath,
             std::string sFileName, bool needGHL )
{
   // 192 is default resolution per beat in some (poorly done) charts, in GH and RB, it's actually 480
   int resolution = 192;
   
   // HOPO frequency varies in FoFiX because HOPO reading sucks in Guitar Hero
   int iHopoResolution = 68;
   
   // special vector for storing special things
   std::vector<std::string> headerInfo(3);
   
   // whether the desired chart was found or not (only applies to reading a single chart)
   bool foundChart = false;
   
   // Screw efficiency, just gonna parse this word by word like I was gonna do with Python
   std::string bufStr(buf);
   std::istringstream iss(bufStr);
   std::string line;
   // Parse line-by-line
   while( std::getline(iss,line) )
   {
      // Remove tabs
      line.erase(std::remove(line.begin(), line.end(), '\t'), line.end());
      std::vector<std::string> vsWords;
      std::istringstream lineIss(line);
      // Separate by spaces
      copy( std::istream_iterator<std::string>(lineIss), std::istream_iterator<std::string>(), back_inserter(vsWords) );
      
      
      /**
       Change the mode once you hit a section tag. Until the actual notes, the tags should always be in the order:
       Song, SyncTrack, Events, then comes the notes which are tagged by difficulty and game mode
       */
      if( vsWords[0].at(0) == '[' ) {
         
         if( vsWords[0].find("Song") != std::string::npos ) {
            // parse header (always parse) all the references are because it's assigning values
            headerInfo = parseHeader(iss, outSong, &resolution, &iHopoResolution, sFilePath, parseSongInfo);
         } else if( vsWords[0].find("SyncTrack") != std::string::npos ) {
            // parse synctrack (only for whole song)
            if(parseSongInfo) parseSyncTrack(iss, outSong.m_SongTiming, resolution);
         } else if( vsWords[0].find("Events") != std::string::npos ) {
            // parse events (only for whole song)
            if(parseSongInfo) parseEvents(iss, outSong, resolution);
            // TODO: maybe add the other steps too?
         } else if( vsWords[0].find("Single") != std::string::npos || vsWords[0].find("GHLGuitar") != std::string::npos ||
                   vsWords[0].find("Double") != std::string::npos || vsWords[0].find("GHLBass") != std::string::npos) {
            bool isGHL = vsWords[0].find("GHL") != std::string::npos;
            bool isBackup = vsWords[0].find("Double") != std::string::npos || vsWords[0].find("Bass") != std::string::npos;
            Difficulty currDiff = parseDifficulty(vsWords[0]);
            if( !parseSongInfo )
            {
               if(currDiff != outSteps.GetDifficulty() || needGHL != isGHL ) {
                  // not the steps we're looking for
                  continue;
               } else {
                  // correct ones, load the step data
                  outSteps.SetNoteData(parseNoteSection(iss, resolution, iHopoResolution, isGHL));
                  outSteps.TidyUpData();
                  foundChart = true;
                  break;
               }
            } else {
               // if we're parsing the whole song, we need all steps
               Steps* pNewNotes = outSong.CreateSteps();
               initSteps(*pNewNotes, currDiff, iss, resolution, iHopoResolution, headerInfo, isGHL, isBackup);
               pNewNotes->SetFilename(sFileName);
               outSong.AddSteps(pNewNotes);
               foundChart = true;
            }
         } else { // don't care about this tag
            continue;
         }

      }
     
   } // End while(line)
   
   if(parseSongInfo) {
      outSong.TidyUpData(false);
   }
   return foundChart;
}

// Returns true if successful, false otherwise
bool ReadFile( std::string sNewPath, Song &outSong, Steps &outSteps, bool parseSongInfo, bool needGHL )
{
   RageFile f;
   /* Open a file. */
   if( !f.Open( sNewPath ) )return false;
   
   // path to the base dir
   std::string sBasePath = sNewPath.substr(0, sNewPath.find_last_of("/")+1);
   
   // allocate a string to hold the file
   std::string FileString;
   FileString.reserve( f.GetFileSize() );
   
   int iBytesRead = f.Read( FileString );
   if( iBytesRead == -1 )return false;
   
   return ReadBuf( FileString.c_str(), iBytesRead, outSong, outSteps, parseSongInfo, sBasePath, sNewPath, needGHL );
}

void CHARTLoader::GetApplicableFiles( const std::string &sPath, std::vector<std::string> &out )
{
   GetDirListing( sPath + std::string("*.chart"), out );
}

bool CHARTLoader::LoadFromDir( const std::string &sDir, Song &out ) {
   LOG->Trace( "CHARTLoader::LoadFromDir(%s)", sDir.c_str() );
   
   std::vector<std::string> arrayCHARTFileNames;
   GetDirListing( sDir + std::string("*.chart"), arrayCHARTFileNames );
   
   // We shouldn't have been called to begin with if there were no CHARTs.
   ASSERT( arrayCHARTFileNames.size() != 0 );
   
   std::string dir = out.GetSongDir();
   
   Steps *tempSteps = NULL;
   
   // Only need to use the first file, since there should only be 1
   return ReadFile( dir+arrayCHARTFileNames[0], out, *tempSteps, true, false );
}

bool CHARTLoader::LoadNoteDataFromSimfile( const std::string & cachePath, Steps &out ) {
   Song *tempSong = NULL;
   // chart loader is ony for guitar mode
   if(out.m_StepsType != StepsType_guitar_solo && out.m_StepsType != StepsType_guitar_solo6 &&
      out.m_StepsType != StepsType_guitar_backup && out.m_StepsType != StepsType_guitar_backup6) return false;
   // This is simple since the path is already given to us
   return ReadFile(cachePath, *tempSong, out, false, (out.m_StepsType == StepsType_guitar_solo6 ||
                                                      out.m_StepsType == StepsType_guitar_backup6));
}
