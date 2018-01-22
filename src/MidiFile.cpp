/*
 * Skeleton for MidiFile.cpp, functions are subject to change and they likely will, this was started a long time ago
 * Will probably scrap this and start from scratch
 */

#include "global.h"
#include "MidiFile.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"


/* Function to read variable length values in files
 * This function will not change the original pointer
 * @param pBuffer - pointer to where the variable length value begins
 * @returns The value stored in the variable length value
 */
uint32 ReadVarLen(const char *&pBuffer)
{
   uint32 value = *pBuffer;
   uint8 c;
   
   // the first bit marks continuation, actual value is stored in next 7 bits
   // and possibly the last 7 bits of the next byte if the continue flag is set
   // and so on
   if(value & 0x80)
   {
      value &= 0x7F; // mask out the continue flag
      do
      {
         pBuffer++;    // increment the buffer marker
         c = *pBuffer;
         value = (value << 7) + (c & 0x7F);
      } while(c & 0x80);   // while there's still a continue flag
   }
   pBuffer++; // increment to point at the next byte after varlength
   
   return value;
}

/* Function to compare values at a pointer to a string of chars
 * Used to determine the validity of a midi file
 * @param pBuffer - pointer to the buffer to compare
 * @param str - the string to compare the values read to
 * @returns True if str matches the first characters in pBuffer, False otherwise
 */
bool CompareToString(const char *&pBuffer, std::string str)
{
   uint8 len = str.length();
   std::string bufStr (pBuffer, len);
   return str.compare(bufStr) == 0;
}

/* Constructor for MidiFile class */
MidiFile::MidiFile(void) {}

/* Destructor for MidiFile class */
MidiFile::~MidiFile(void) {
   
}

// see this:
// http://stackoverflow.com/questions/6043689/c-what-is-pointer-new-type-as-oppose-to-pointer-new-type
// should probably do something similar for the pointer to pointers of midievents


/// TODO: All below
/* Loads a mid file in to a MidiFile object
 * @param pFile - pointer to the start of the midi file
 * @param size - size of the file in bytes
 * @returns MidiFile object with the information from the mid file
 */
MIDIFile* LoadMidiFromFile(const char *pFile, size_t size)
{
   if(!size)
      pFile = MFFileSystem_Load(pFile, &size);
   
   if(!pFile)
      return NULL;
   
   const char *pOffset = pFile;
   
   // "RIFF" is a (useless) wrapper sometimes applied to midis
   if(CompareToString(pFile, "RIFF"))
   {
      pOffset += 8;
      // if the next bytes are not "RMID", it is not a midi file
      if(CompareToString(pFile, "RMID"))
      {
         return NULL;
      }
      pOffset += 4;
   }
   
   Header_Chunk *pHd = (Header_Chunk*)pOffset;
   // if the midi header is not found, it is not a midi file
   if(CompareToString(pFile, "MThd"))
   {
      return NULL;
   }
   
   // I'm going to assume endianness is not a problem...
   //MFEndian_BigToHost(&pHd->length);
   //MFEndian_BigToHost(&pHd->format);
   //MFEndian_BigToHost(&pHd->numTracks);
   //MFEndian_BigToHost(&pHd->ticksPerBeat);
   
   MIDIFile *pMidiFile = new MidiFile();
   
   pMidiFile->name[0] = 0;
   pMidiFile->format = pHd->format;
   pMidiFile->ticksPerBeat = pHd->ticksPerBeat;
   pMidiFile->numTracks = pHd->numTracks;
   pMidiFile->tracks.resize(pHd->numTracks);
   
   // we will only deal with type 1 midi files here..
   //MFDebug_Assert(pHd->format == 1, "Invalid midi type.");
   
   pOffset += 8 + pHd->length;
   
   for(int t = 0; t < pMidiFile->numTracks && pOffset < pFile + size; ++t)
   {
      Track_Chunk track;
      memcpy(&track.signature, pOffset, sizeof(track.id));
      pOffset += sizeof(track.signature);
      memcpy(&track.length, pOffset, sizeof(track.length));
      pOffset += sizeof(track.length);
      /// TODO: Necessary?
      //MFEndian_BigToHost(&track.length);
      
      /// TODO: is this valid? uint32 compare?
      if(CompareToString(track.signature, "MTrk"))
      {
         const char *pTrk = pOffset;
         uint32 tick = 0;
         
         while(pTrk < pOffset + track.length)
         {
            uint32 delta = ReadVarLen(pTrk);
            tick += delta;
            uint8 status = *(uint8*)pTrk++;
            
            MidiEvent *pEvent = NULL;
            
            if(status == MidiEventType::MidiEvent_Meta)
            {
               // meta event
               uint8 type = *(uint8*)pTrk++;
               uint32 bytes = ReadVarLen(pTrk);
               
               // read event
               switch(type)
               {
                  case MidiMeta_SequenceNumber:
                  {
                     static int sequence = 0;
                     
                     /// TODO: change to alloc
                     MIDIEvent_SequenceNumber *pSeq = (MIDIEvent_SequenceNumber*)MFHeap_Alloc(sizeof(MIDIEvent_SequenceNumber));
                     pEvent = pSeq;
                     
                     if(!bytes)
                        pSeq->sequence = sequence++;
                     else
                     {
                        uint16 seq;
                        /// TODO: change to memcopy
                        MFCopyMemory(&seq, pTrk, 2);
                        /// TODO: ???
                        //MFEndian_BigToHost(&seq);
                        pSeq->sequence = (int)seq;
                     }
                     break;
                  }
                  case MidiMeta_Text:
                  case MidiMeta_Copyright:
                  case MidiMeta_TrackName:
                  case MidiMeta_Instrument:
                  case MidiMeta_Lyric:
                  case MidiMeta_Marker:
                  case MidiMeta_CuePoint:
                  case MidiMeta_PatchName:
                  case MidiMeta_PortName:
                  {
                     /// TODO: malloc
                     MIDIEvent_Text *pText = (MIDIEvent_Text*)MFHeap_Alloc(sizeof(MIDIEvent_Text));
                     pEvent = pText;
                     /// TODO: memcopy
                     MFCopyMemory(pText->buffer, pTrk, bytes);
                     pText->buffer[bytes] = 0;
                     break;
                  }
                  case MidiMeta_EndOfTrack:
                  {
                     /// TODO: malloc
                     MIDIEvent *pE = (MIDIEvent*)MFHeap_Alloc(sizeof(MIDIEvent));
                     pEvent = pE;
                     pTrk = pOffset + track.length;
                     break;
                  }
                  case MidiMeta_Tempo:
                  {
                     /// TODO: malloc
                     MIDIEvent_Tempo *pTempo = (MIDIEvent_Tempo*)MFHeap_Alloc(sizeof(MIDIEvent_Tempo));
                     pEvent = pTempo;
                     pTempo->microsecondsPerBeat = ((uint8*)pTrk)[0] << 16;
                     pTempo->microsecondsPerBeat |= ((uint8*)pTrk)[1] << 8;
                     pTempo->microsecondsPerBeat |= ((uint8*)pTrk)[2];
                     pTempo->BPM = 60000000.0f/(float)pTempo->microsecondsPerBeat;
                     break;
                  }
                  case MidiMeta_SMPTE:
                  {
                     /// TODO: malloc
                     MIDIEvent_SMPTE *pSMPTE = (MIDIEvent_SMPTE*)MFHeap_Alloc(sizeof(MIDIEvent_SMPTE));
                     pEvent = pSMPTE;
                     pSMPTE->hours = ((uint8*)pTrk)[0];
                     pSMPTE->minutes = ((uint8*)pTrk)[1];
                     pSMPTE->seconds = ((uint8*)pTrk)[2];
                     pSMPTE->frames = ((uint8*)pTrk)[3];
                     pSMPTE->subFrames = ((uint8*)pTrk)[4];
                     break;
                  }
                  case MidiMeta_TimeSignature:
                  {
                     /// TODO: malloc
                     MIDIEvent_TimeSignature *pTS = (MIDIEvent_TimeSignature*)MFHeap_Alloc(sizeof(MIDIEvent_TimeSignature));
                     pEvent = pTS;
                     pTS->numerator = ((uint8*)pTrk)[0];
                     pTS->denominator = ((uint8*)pTrk)[1];
                     pTS->clocks = ((uint8*)pTrk)[2];
                     pTS->d = ((uint8*)pTrk)[3];
                     break;
                  }
                  case MidiMeta_KeySignature:
                  {
                     /// TODO: malloc
                     MIDIEvent_KeySignature *pKS = (MIDIEvent_KeySignature*)MFHeap_Alloc(sizeof(MIDIEvent_KeySignature));
                     pEvent = pKS;
                     pKS->sf = ((uint8*)pTrk)[0];
                     pKS->minor = ((uint8*)pTrk)[1];
                     break;
                  }
                  case MidiMeta_Custom:
                  {
                     /// TODO: malloc maybe make a function?... probably no...
                     MIDIEvent_Custom *pCustom = (MIDIEvent_Custom*)MFHeap_Alloc(sizeof(MIDIEvent_Custom));
                     pEvent = pCustom;
                     pCustom->pData = pTrk;
                     pCustom->size = bytes;
                     break;
                  }
               }
               
               if(pEvent)
                  pEvent->subType = type;
               
               pTrk += bytes;
            }
            /// TODO: comments, what is going on here????
            else if(status == MidiEventType::MidiEvent_SYSEX || status == MidiEventType::MidiEvent_SYSEX2)
            {
               uint32 bytes = ReadVarLen(pTrk);
               
               // SYSEX event...
               //MFDebug_Log(2, "Encountered SYSEX event...");
               
               pTrk += bytes;
            }
            else
            {
               static int lastStatus = 0;
               
               if(status < 0x80)
               {
                  --pTrk;
                  status = lastStatus;
               }
               
               lastStatus = status;
               
               int eventType = status&0xF0;
               
               int param1 = ReadVarLen(pTrk);
               int param2 = 0;
               /// TODO: Why is this here? what does it do?
               if(eventType != MidiNote_ProgramChange && eventType != MidiNote_ChannelAfterTouch)
                  param2 = ReadVarLen(pTrk);
               
               switch(eventType)
               {
                  case MidiNote_NoteOn:
                  case MidiNote_NoteOff:
                  {
                     /// TODO: malloc
                     MidiEvent_Note *pNote = (MIDIEvent_Note*)MFHeap_Alloc(sizeof(MIDIEvent_Note));
                     pEvent = pNote;
                     pNote->event = status&0xF0;
                     pNote->channel = status&0x0F;
                     pNote->note = param1;
                     pNote->velocity = param2;
                     break;
                  }
               }
               
               if(pEvent)
               {
                  pEvent->subType = status;
                  status = MidiEvent_Note;
               }
            }
            
            // append event to track
            if(pEvent)
            {
               pEvent->tick = tick;
               pEvent->delta = delta;
               pEvent->type = status;
               pEvent->pNext = NULL;
               
               MidiEvent *pEv = pMidiFile->tracks[t];
               
               if(!pEv)
               {
                  pMidiFile->tracks[t] = pEvent;
               }
               else
               {
                  while(pEv->pNext)
                     pEv = pEv->pNext;
                  pEv->pNext = pEvent;
               }
            }
         }
      }
      
      pOffset += track.length;
   }
   
   return pMidiFile;
}
