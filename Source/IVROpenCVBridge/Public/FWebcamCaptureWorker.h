// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h" // Para FThreadSafeBool
#include "HAL/ThreadSafeCounter.h" // Para FThreadSafeCounter (resolução)
#include "Containers/Queue.h"   // Para TQueue
#include "IVRTypes.h" // Para FIVR_VideoFrame
#include "IVRFramePool.h" // Para UIVRFramePool
#include <atomic> // Necessário para std::atomic
#include <string> // Inclusão explícita para std::string


#include "IVROpenCVBridge.h"

// --- INÍCIO DA ALTERAÇÃO: Ocultar tipos OpenCV dos headers públicos ---
// Não inclui headers de OpenCV aqui.
namespace cv { class VideoCapture; } // Forward declaration para cv::VideoCapture

// Define um tipo para o API Preference do OpenCV.
// Usamos 'int' como fallback seguro, já que o enum real 'cv::VideoCaptureAPIs'
// não deve ser exposto aqui.
using VideoCaptureAPIs = int;
// --- FIM DA ALTERAÇÃO ---

/**
 * @brief Worker thread para capturar frames de uma webcam usando OpenCV.
 * Esta classe opera em um thread separado para evitar bloqueios do Game Thread.
 */
class IVROPENCVBRIDGE_API FWebcamCaptureWorker : public FRunnable
{
public:
    /**
     * @brief Construtor do worker.
     * @param InFramePool Referência ao pool de frames para adquirir buffers.
     * @param InQueue Fila para enfileirar frames capturados.
     * @param InStopFlag Flag para sinalizar a parada do thread.
     * @param InNewFrameEvent Evento para sinalizar novos frames.
     * @param InDeviceIndex Índice da webcam a ser utilizada.
     * @param InWidth Largura desejada para a captura.
     * @param InHeight Altura desejada para a captura.
     * @param InFPS FPS desejado para a captura.
     * @param InApiPreference Preferência de API para OpenCV (e.g., cv::CAP_DSHOW como int).
     */
    FWebcamCaptureWorker(UIVRFramePool* InFramePool, TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& InQueue, FThreadSafeBool& InStopFlag, FEvent* InNewFrameEvent, int32 InDeviceIndex, int32 InWidth, int32 InHeight, float InFPS,
        VideoCaptureAPIs InApiPreference
    );
    
    /**
     * @brief Destrutor do worker, libera o VideoCapture.
     */
    virtual ~FWebcamCaptureWorker();

    /**
     * @brief Inicializa o worker. Não abre a webcam aqui para evitar bloqueios no início da thread.
     * @return true.
     */
    virtual bool Init() override;
    /**
     * @brief Loop principal do worker thread para capturar frames.
     * Abertura da webcam ocorre aqui para garantir que bloqueios aconteçam no worker thread.
     * @return 0 ao sair, ou 1 se a webcam não puder ser aberta.
     */
    virtual uint32 Run() override;

    /**
     * @brief Sinaliza ao worker thread para parar.
     */
    virtual void Stop() override;

    /**
     * @brief Limpeza final ao sair do thread.
     */
    virtual void Exit() override;

public: 
    // Membros para armazenar a resolução real da captura, atualizados no Run()
    FThreadSafeCounter ActualFrameWidth;
    FThreadSafeCounter ActualFrameHeight;

private:
    TQueue<FIVR_VideoFrame, EQueueMode::Mpsc>& CapturedFrameQueue; // Referência à fila de frames
    UIVRFramePool* FramePool; // Referência ao pool para adquirir buffers
    FThreadSafeBool& bShouldStop; // Referência para a flag de parada do thread
    FEvent* NewFrameEvent; // Referência ao evento de sinalização
    int32 DeviceIndex; // Índice da webcam a ser utilizada
    int32 DesiredWidth; // Largura desejada para a captura
    int32 DesiredHeight; // Altura desejada para a captura
    float DesiredFPS; // FPS desejado para a captura

    // --- INÍCIO DA ALTERAÇÃO: cv::VideoCapture agora é um ponteiro e ApiPreference é int ---
    cv::VideoCapture* OpenCVWebcamCapture; // Objeto de captura de vídeo do OpenCV, agora ponteiro
    int ApiPreference; // Preferência de API para OpenCV (int)
    // --- FIM DA ALTERAÇÃO ---
};