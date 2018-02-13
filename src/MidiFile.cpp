/*
 * MidiFile parser for all your Midi File parsing needs
 * >>does not record all midi events, such as certain special aftertouch notes and such<<
 */

#include "global.h"
#include "MidiFile.h"
#include "RageFile.h"
#include "RageLog.h"
#include "RageUtil.h"
#include <string>


/* Function to read variable length values in files
 * This function will not change the original pointer
 * @param pBuffer - pointer to where the variable length value begins
 * @returns The value stored in the variable length value
 */
uint32_t ReadVarLen(const char *&pBuffer)
{
   uint32_t value = *pBuffer;
   uint8_t c;
   
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
template <typename midY>
bool CompareToString(midY *pBuffer, std::string str)
{
   uint8_t len = str.length();
   const char *tempPtr = (const char*)pBuffer;
   std::string bufStr (tempPtr, len);
   return str.compare(bufStr) == 0;
}

/* Function used to swap the bytes around so they are in the correct endianness
 * This function changes the pointer it was passed to point to a new variable
 * @param pBytes - pointer to the bytes to flip
 */
template <typename midT>
void FlipEndianness(midT *pBytes)
{
   size_t tempSize = sizeof(midT);
   char holder[tempSize];
   char *tempData = (char*)pBytes;
   
   for (int i = 0; i < tempSize; i++) {
      holder[i] = tempData[tempSize - 1 - i];
   }
   
   *pBytes = *(midT*)holder;
}

/* Constructor for MidiFile class */
MidiFile::MidiFile(void) {}

/* Destructor for MidiFile class */
MidiFile::~MidiFile(void) {
   
}

/* Loads a mid file in to a MidiFile object
 * @param pFile - pointer to the start of the midi file
 * @param size - size of the file in bytes
 * @returns MidiFile object with the information from the mid file
 */
MidiFile* ParseMidi(const char *pFile, size_t size)
{
   if(!pFile)
      return NULL;
   
   const char *pOffset = pFile;
   
   // "RIFF" is a (useless) wrapper sometimes applied to midis
   if(CompareToString(pFile, "RIFF"))
   {
      pOffset += 8;
      // if the next bytes are not "RMID", it is not a midi file
      if(!CompareToString(pFile, "RMID"))
      {
         return NULL;
      }
      pOffset += 4;
   }
   
   MidiFile::Header_Chunk *pHd = (MidiFile::Header_Chunk*)pOffset;
   // if the midi header is not found, it is not a midi file
   if(!CompareToString(pFile, "MThd"))
   {
      return NULL;
   }
   
   // Reverse the endian bytes of data
   FlipEndianness(&pHd->length);
   FlipEndianness(&pHd->format);
   FlipEndianness(&pHd->numTracks);
   FlipEndianness(&pHd->ticksPerBeat);
   
   MidiFile *pMidiFile = new MidiFile();
   
   pMidiFile->name[0] = 0;
   pMidiFile->format = pHd->format;
   pMidiFile->ticksPerBeat = pHd->ticksPerBeat;
   pMidiFile->numTracks = pHd->numTracks;
   pMidiFile->tracks.resize(pHd->numTracks);
   
   pOffset += 8 + pHd->length;
   
   for(int t = 0; t < pMidiFile->numTracks && pOffset < pFile + size; ++t)
   {
      MidiFile::Track_Chunk track;
      memcpy(&track.signature, pOffset, sizeof(track.signature));
      pOffset += sizeof(track.signature);
      memcpy(&track.length, pOffset, sizeof(track.length));
      pOffset += sizeof(track.length);

      FlipEndianness(&track.length);
      
      if(CompareToString(&track.signature, "MTrk"))
      {
         const char *pTrk = pOffset;
         uint32_t tick = 0;
         
         // this is most certainly null, but get it just in case
         MidiFile::MidiEvent *pEv = pMidiFile->tracks[t];
         
         while(pTrk < pOffset + track.length)
         {
            uint32_t delta = ReadVarLen(pTrk);
            tick += delta;
            uint8_t status = *(uint8_t*)pTrk++;
            
            MidiFile::MidiEvent *pEvent = NULL;
            
            if(status == MidiFile::MidiEventType::MidiEventType_Meta)
            {
               // meta event
               uint8_t type = *(uint8_t*)pTrk++;
               uint32_t bytes = ReadVarLen(pTrk);
               
               // read event
               switch(type)
               {
                  case MidiFile::MidiMeta_SequenceNumber:
                  {
                     static int sequence = 0;
                     
                     MidiFile::MidiEvent_SequenceNumber *pSeq = new MidiFile::MidiEvent_SequenceNumber;
                     pEvent = pSeq;
                     
                     if(!bytes)
                        pSeq->sequence = sequence++;
                     else
                     {
                        uint16_t seq;
                        memcpy(&seq, pTrk, 2);
                        FlipEndianness(&seq);
                        pSeq->sequence = (int)seq;
                     }
                     break;
                  }
                  case MidiFile::MidiMeta_Text:
                  case MidiFile::MidiMeta_Copyright:
                  case MidiFile::MidiMeta_TrackName:
                  case MidiFile::MidiMeta_Instrument:
                  case MidiFile::MidiMeta_Lyric:
                  case MidiFile::MidiMeta_Marker:
                  case MidiFile::MidiMeta_CuePoint:
                  case MidiFile::MidiMeta_PatchName:
                  case MidiFile::MidiMeta_PortName:
                  {
                     MidiFile::MidiEvent_Text *pText = new MidiFile::MidiEvent_Text;
                     pEvent = pText;
                     memcpy(pText->buffer, pTrk, bytes);
                     pText->buffer[bytes] = 0;
                     break;
                  }
                  case MidiFile::MidiMeta_EndOfTrack:
                  {
                     MidiFile::MidiEvent *pE = new MidiFile::MidiEvent;
                     pEvent = pE;
                     pTrk = pOffset + track.length;
                     break;
                  }
                  case MidiFile::MidiMeta_Tempo:
                  {
                     MidiFile::MidiEvent_Tempo *pTempo = new MidiFile::MidiEvent_Tempo;
                     pEvent = pTempo;
                     pTempo->microsecondsPerBeat = ((uint8_t*)pTrk)[0] << 16;
                     pTempo->microsecondsPerBeat |= ((uint8_t*)pTrk)[1] << 8;
                     pTempo->microsecondsPerBeat |= ((uint8_t*)pTrk)[2];
                     pTempo->BPM = 60000000.0f/(float)pTempo->microsecondsPerBeat;
                     break;
                  }
                  case MidiFile::MidiMeta_SMPTE:
                  {
                     MidiFile::MidiEvent_SMPTE *pSMPTE = new MidiFile::MidiEvent_SMPTE;
                     pEvent = pSMPTE;
                     pSMPTE->hours = ((uint8_t*)pTrk)[0];
                     pSMPTE->minutes = ((uint8_t*)pTrk)[1];
                     pSMPTE->seconds = ((uint8_t*)pTrk)[2];
                     pSMPTE->frames = ((uint8_t*)pTrk)[3];
                     pSMPTE->subFrames = ((uint8_t*)pTrk)[4];
                     break;
                  }
                  case MidiFile::MidiMeta_TimeSignature:
                  {
                     MidiFile::MidiEvent_TimeSignature *pTS = new MidiFile::MidiEvent_TimeSignature;
                     pEvent = pTS;
                     pTS->numerator = ((uint8_t*)pTrk)[0];
                     pTS->denominator = 1 << ((uint8_t*)pTrk)[1];
                     pTS->clocks = ((uint8_t*)pTrk)[2];
                     pTS->d = ((uint8_t*)pTrk)[3];
                     break;
                  }
                  case MidiFile::MidiMeta_KeySignature:
                  {
                     MidiFile::MidiEvent_KeySignature *pKS = new MidiFile::MidiEvent_KeySignature;
                     pEvent = pKS;
                     pKS->sf = ((uint8_t*)pTrk)[0];
                     pKS->minor = ((uint8_t*)pTrk)[1];
                     break;
                  }
                  case MidiFile::MidiMeta_Custom:
                  {
                     MidiFile::MidiEvent_Custom *pCustom = new MidiFile::MidiEvent_Custom;
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
            else if(status == MidiFile::MidiEventType::MidiEventType_SYSEX || status == MidiFile::MidiEventType::MidiEventType_SYSEX2)
            {
               // Ignore sysex events for now
               uint32_t bytes = ReadVarLen(pTrk);
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
               // param2 will contain useful info, if the event isn't programChange or channelAfterTouch
               if(eventType != MidiFile::MidiNote_ProgramChange && eventType != MidiFile::MidiNote_ChannelAfterTouch)
                  param2 = ReadVarLen(pTrk);
               
               switch(eventType)
               {
                  case MidiFile::MidiNote_NoteOn:
                  case MidiFile::MidiNote_NoteOff:
                  {
                     MidiFile::MidiEvent_Note *pNote = new MidiFile::MidiEvent_Note;
                     pEvent = pNote;
                     pNote->event = status&0xF0;
                     pNote->channel = status&0x0F;
                     pNote->note = param1;
                     pNote->velocity = param2;
                     status &= 0xF0;
                     break;
                  }
               }
               
               if(pEvent)
               {
                  pEvent->subType = status;
                  status = MidiFile::MidiEventType_Note;
               }
            }
            
            // append event to track
            if(pEvent)
            {
               pEvent->tick = tick;
               pEvent->delta = delta;
               pEvent->type = status;
               pEvent->pNext = NULL;
               
               if(!pEv)
               {
                  // set the first event
                  pMidiFile->tracks[t] = pEvent;
               }
               else
               {
                  // set the next event
                  pEv->pNext = pEvent;
               }
               // point to the most recent event
               pEv = pEvent;
            }
         }
      }
      
      pOffset += track.length;
   }
   
   return pMidiFile;
}

MidiFile* ReadMidiFile(std::string fileName)
{
   RageFile f;
   /* Open a file. */
   if( !f.Open( fileName ) )return nullptr;
   
   // allocate a string to hold the file
   std::string FileString;
   FileString.reserve( f.GetFileSize() );
   
   int iBytesRead = f.Read( FileString );
   if( iBytesRead == -1 )return nullptr;
   
   return ParseMidi(FileString.c_str(), iBytesRead);
}

