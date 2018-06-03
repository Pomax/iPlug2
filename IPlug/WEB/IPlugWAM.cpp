/*
 ==============================================================================
 
 This file is part of the iPlug 2 library
 
 Oli Larkin et al. 2018 - https://www.olilarkin.co.uk
 
 iPlug 2 is an open source library subject to commercial or open-source
 licensing.
 
 The code included in this file is provided under the terms of the WDL license
 - https://www.cockos.com/wdl/
 
 ==============================================================================
 */

#include "IPlugWAM.h"

IPlugWAM::IPlugWAM(IPlugInstanceInfo instanceInfo, IPlugConfig c)
  : IPlugAPIBase(c, kAPIWAM)
  , IPlugProcessor<float>(c, kAPIWAM)
{
  int nInputs = MaxNChannels(ERoute::kInput), nOutputs = MaxNChannels(ERoute::kOutput);

  _SetChannelConnections(ERoute::kInput, 0, nInputs, true);
  _SetChannelConnections(ERoute::kOutput, 0, nOutputs, true);
}

const char* IPlugWAM::init(uint32_t bufsize, uint32_t sr, void* pDesc)
{
  DBGMSG("init\n");

  _SetSampleRate(sr);
  _SetBlockSize(bufsize);

  DBGMSG("%i %i\n", sr, bufsize);

  WDL_String json;
  json.Set("{\n");
  json.AppendFormatted(8192, "\"audio\": { \"inputs\": [{ \"id\":0, \"channels\":%i }], \"outputs\": [{ \"id\":0, \"channels\":%i }] },\n", MaxNChannels(ERoute::kInput), MaxNChannels(ERoute::kOutput));
  json.AppendFormatted(8192, "\"parameters\": [\n");

  for (int idx = 0; idx < NParams(); idx++)
  {
    IParam* pParam = GetParam(idx);
    pParam->GetJSON(json, idx);

    if(idx < NParams()-1)
      json.AppendFormatted(8192, ",\n");
    else
      json.AppendFormatted(8192, "\n");
  }

  json.Append("]\n}");

  //TODO: correct place? - do we need a WAM reset message?
  OnReset();

  return json.Get();
}

void IPlugWAM::onProcess(WAM::AudioBus* pAudio, void* pData)
{
  _SetChannelConnections(ERoute::kInput, 0, MaxNChannels(ERoute::kInput), false); //TODO: go elsewhere - enable inputs
  _SetChannelConnections(ERoute::kOutput, 0, MaxNChannels(ERoute::kOutput), true); //TODO: go elsewhere
  _AttachBuffers(ERoute::kInput, 0, NChannelsConnected(ERoute::kInput), pAudio->inputs, GetBlockSize());
  _AttachBuffers(ERoute::kOutput, 0, NChannelsConnected(ERoute::kOutput), pAudio->outputs, GetBlockSize());
  _ProcessBuffers((float) 0.0f, GetBlockSize());
  
  if(mBlockCounter == 0)
  {
    // TODO: IPlugAPIBase:OnIdle() should be called on the main thread - how to do that in audio worklet processor?
    OnIdle();
    
    mBlockCounter = 8; // 8 * 128 samples = 23ms @ 44100 sr
  }
  
  mBlockCounter--;
}

void IPlugWAM::onMessage(char* verb, char* res, double data)
{
  if(strcmp(verb, "SMMFUI") == 0)
  {
    uint8_t data[3];
    char* pChar = strtok(res, ":");
    int i = 0;
    while (pChar != nullptr)
    {
      data[i++] = atoi(pChar);
      pChar = strtok (nullptr, ":");
    }
    
    IMidiMsg msg = {0, data[0], data[1], data[2]};
    ProcessMidiMsg(msg); // TODO: should queue to mMidiMsgsFromEditor?
  }
}

//void IPlugWAM::onMessage(char* verb, char* res, char* data)
//{
//  DBGMSG("IPlugWAM2:: onMessage %s %s %s\n", verb, res, data);
//}
//
//void IPlugWAM::onMessage(char* verb, char* res, void* data, uint32_t size)
//{
//  DBGMSG("IPlugWAM3:: onMessage %s %s VOID\n", verb, res);
//}

void IPlugWAM::onMidi(byte status, byte data1, byte data2)
{
//   DBGMSG("onMidi\n");
  IMidiMsg msg = {0, status, data1, data2};
  ProcessMidiMsg(msg); // onMidi is not called on HPT. We could queue things up, but just process the message straightaway for now
  //mMidiMsgsFromProcessor.Push(msg);
  
  WDL_String dataStr;
  dataStr.SetFormatted(16, "%i:%i:%i", msg.mStatus, msg.mData1, msg.mData2);
  
  // TODO: in the future this will be done via shared array buffer
  // if onMidi ever gets called on HPT, should defer via queue
  postMessage("SMMFD", dataStr.Get(), "");
}

void IPlugWAM::onParam(uint32_t idparam, double value)
{
//  DBGMSG("IPlugWAM:: onParam %i %f\n", idparam, value);
  SetParameterValue(idparam, value);
}

void IPlugWAM::onSysex(byte* msg, uint32_t size)
{
  ISysEx sysex = {0, msg, (int) size };
  ProcessSysEx(sysex);
}

void IPlugWAM::SetControlValueFromDelegate(int controlTag, double normalizedValue)
{
  WDL_String propStr;
  WDL_String dataStr;

  propStr.SetFormatted(16, "%i", controlTag);
  dataStr.SetFormatted(16, "%f", normalizedValue);

  // TODO: in the future this will be done via shared array buffer
  postMessage("SCVFD", propStr.Get(), dataStr.Get());
}

void IPlugWAM::SendControlMsgFromDelegate(int controlTag, int messageTag, int dataSize, const void* pData)
{
  WDL_String propStr;
  propStr.SetFormatted(16, "%i:%i", controlTag, messageTag);
  
  // TODO: in the future this will be done via shared array buffer
  postMessage("SCMFD", propStr.Get(), pData, (uint32_t) dataSize);
}

