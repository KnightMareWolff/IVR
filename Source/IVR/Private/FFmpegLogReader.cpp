// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#include "FFmpegLogReader.h"
#include "Containers/StringConv.h"
#include "IVR.h" // Inclua a LogCategory

// NOVO: Inicializa o LogPrefix no construtor
FFMpegLogReader::FFMpegLogReader(void* InReadPipe, const FString& InLogPrefix)
    : ReadPipe(InReadPipe)
    , Thread(nullptr)
    , bShouldStop(false)
    , LogPrefix(InLogPrefix) // Inicializa o novo membro
{
}

FFMpegLogReader::~FFMpegLogReader()
{
    EnsureCompletion();
    // NUNCA FECHE O ReadPipe AQUI. Ele � de propriedade do IVRVideoEncoder (ou UIVRRecordingSession)
    // e ser� fechado por ele. O FFMpegLogReader apenas o usa.
}

bool FFMpegLogReader::Init()
{
    UE_LOG(LogIVR, Log, TEXT("FFMpegLogReader (%s): Thread initialized."), *LogPrefix);
    return true;
}

uint32 FFMpegLogReader::Run()
{
    UE_LOG(LogIVR, Log, TEXT("FFMpegLogReader (%s): Thread running."), *LogPrefix);
    
    // Continua lendo do pipe enquanto a thread n�o for parada
    while (!bShouldStop) // USO CORRETO: FThreadSafeBool � implicitamente convers�vel para bool
    {
        // FPlatformProcess::ReadPipe agora retorna FString.
        // Ele bloqueia at� que haja dados, ou o pipe seja fechado/processo termine.
        FString Output = FPlatformProcess::ReadPipe(ReadPipe);

        // Se a string n�o estiver vazia, significa que leu dados
        if (!Output.IsEmpty())
        {
            Output.TrimEndInline(); // Remove quebras de linha e espa�os em branco no final
            // NOVO: Usa o prefixo no log
            UE_LOG(LogIVR, Log, TEXT("%s: %s"), *LogPrefix, *Output);
        }
        else
        {
            // Se Output estiver vazia, pode significar EOF do pipe ou que n�o h� mais dados no momento.
            // Um pequeno delay evita um loop muito apertado e economiza CPU.
            FPlatformProcess::Sleep(0.01f); // Espera 10ms
        }
    }
    UE_LOG(LogIVR, Log, TEXT("FFMpegLogReader (%s): Thread stopped."), *LogPrefix);
    return 0;
}

void FFMpegLogReader::Stop()
{
    bShouldStop.AtomicSet(true); // USO CORRETO: AtomicSet para escrever
}

void FFMpegLogReader::Exit()
{
    // Limpeza final, se necess�rio
    UE_LOG(LogIVR, Log, TEXT("FFMpegLogReader (%s): Thread exited."), *LogPrefix);
}

void FFMpegLogReader::Start()
{
    Thread = FRunnableThread::Create(this, *FString::Printf(TEXT("FFmpegLogReaderThread_%s"), *LogPrefix.Replace(TEXT(" "), TEXT(""))), 0, TPri_BelowNormal);
}

void FFMpegLogReader::EnsureCompletion()
{
    if (Thread)
    {
        Stop(); // Sinaliza para parar
        Thread->WaitForCompletion(); // Espera a thread terminar
        delete Thread; // Libera a mem�ria da thread
        Thread = nullptr;
    }
}

