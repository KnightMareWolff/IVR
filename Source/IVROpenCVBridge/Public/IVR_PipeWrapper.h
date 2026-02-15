// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of Williäm Wolff and protected by copyright law.
// Proibited copy or distribution without expressed authorization of the Author.
#pragma once

#include "CoreMinimal.h"
#include "IVROpenCVBridge.h" // Inclua o log principal do IVR, se LogIVRPipeWrapper usar LogIVR
#include "IVRTypes.h" // Inclui a USTRUCT FIVR_PipeSettings
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <stdio.h> // Para GetLastError
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_LINUX || PLATFORM_MAC
#include <sys/stat.h>   // Para mkfifo
#include <fcntl.h>      // Para open, O_RDWR, O_CREAT
#include <unistd.h>     // Para close, write
#endif

//#include "IVR_PipeWrapper.generated.h"


// Forward declarations para tipos OS-específicos
#if PLATFORM_WINDOWS
typedef void* IVR_NativePipeHandle; // HANDLE é void*
#else
typedef int IVR_NativePipeHandle; // file descriptor é int
#endif

// Define um valor inválido para o handle nativo
#define IVR_INVALID_NATIVE_PIPE_HANDLE ((IVR_NativePipeHandle)-1)

// Definição de LogCategory para FIVR_PipeWrapper (se usar LogIVR)
DECLARE_LOG_CATEGORY_EXTERN(LogIVRPipeWrapper, Log, All);



/**
 * Encapsula um Named Pipe multiplataforma (HANDLE no Windows, file descriptor no Linux).
 */
struct IVROPENCVBRIDGE_API FIVR_PipeWrapper
{
public:

     FIVR_PipeWrapper();
    ~FIVR_PipeWrapper();

     /**
    * Cria e abre o Named Pipe (Windows) ou FIFO (POSIX).
    * @param Settings As configurações para criar o pipe.
    * @param SessionID Um ID único para esta sessão, para garantir nomes de pipe únicos.
    * @param InWidth Largura do vídeo (usada para calcular o tamanho do buffer do pipe).
    * @param InHeight Altura do vídeo (usada para calcular o tamanho do buffer do pipe).
    * @return true se o pipe foi criado e aberto com sucesso, false caso contrário.
    */
    bool Create(const FIVR_PipeSettings& Settings, const FString& SessionID, int32 InWidth = 0, int32 InHeight = 0);
    /**
     * Tenta conectar-se ao Named Pipe. Este método bloqueará até que um cliente
     * (e.g., FFmpeg) se conecte ao pipe.
     * Deve ser chamado DEPOIS que o pipe é criado (com Create()) e DEPOIS que o cliente
     * que vai ler/escrever no pipe é lançado.
     * @return true se a conexão foi bem-sucedida, false caso contrário.
     */
    bool Connect(); // <-- NOVO MÉTODO

    /**
     * Escreve dados no pipe.
     * @param Data Ponteiro para os dados a serem escritos.
     * @param Size O número de bytes a serem escritos.
     * @return O número de bytes realmente escritos, ou -1 em caso de erro.
     */
    int32 Write(const uint8* Data, int32 NumBytes);

    /**
     * Fecha o pipe e libera seus recursos.
     * Para FIFOs POSIX, isso também remove o arquivo do sistema de arquivos.
     */
    void Close();

    /**
     * Verifica se o pipe está aberto e válido.
     */
    bool IsValid() const;

    /**
     * Retorna o caminho completo do pipe (e.g., "\.\pipe\MyPipe" ou "/tmp/MyPipe").
     */
    FString GetFullPipeName() const;

private:

#if PLATFORM_WINDOWS
    HANDLE PipeHandle; // HANDLE para Windows Named Pipe
#elif PLATFORM_LINUX || PLATFORM_MAC
    int FileDescriptor; // Descritor de arquivo para FIFO POSIX
#endif

    FIVR_PipeSettings PipeSettings;

    FString FullPipePath; // Caminho completo para o pipe
    bool bIsCreatedAndConnected; // Indica se o pipe foi criado e está pronto para uso

    // Desabilitar cópia e atribuição
    FIVR_PipeWrapper(const FIVR_PipeWrapper&) = delete;
    FIVR_PipeWrapper& operator=(const FIVR_PipeWrapper&) = delete;
};