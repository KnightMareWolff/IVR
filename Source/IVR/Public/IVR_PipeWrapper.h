// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of WilliÃ¤m Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------
#pragma once

#include "CoreMinimal.h"
#include "IVR.h"
#include "Core/IVRTypes.h" // Inclui a USTRUCT FIVR_PipeSettings

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

// Forward declarations para tipos OS-espec�ficos
#if PLATFORM_WINDOWS
typedef void* IVR_NativePipeHandle; // HANDLE � void*
#else
typedef int IVR_NativePipeHandle; // file descriptor � int
#endif

// Define um valor inv�lido para o handle nativo
#define IVR_INVALID_NATIVE_PIPE_HANDLE ((IVR_NativePipeHandle)-1)

// Defini��o de LogCategory para FIVR_PipeWrapper
DECLARE_LOG_CATEGORY_EXTERN(LogIVRPipeWrapper, Log, All);

/**
 * Encapsula um Named Pipe multiplataforma (HANDLE no Windows, file descriptor no Linux).
 */
struct IVR_API FIVR_PipeWrapper
{
public:

     FIVR_PipeWrapper();
    ~FIVR_PipeWrapper();

     /**
    * Cria e abre o Named Pipe (Windows) ou FIFO (POSIX).
    * @param Settings As configura��es para criar o pipe.
    * @param SessionID Um ID �nico para esta sess�o, para garantir nomes de pipe �nicos.
    * @param InWidth Largura do v�deo (usada para calcular o tamanho do buffer do pipe).
    * @param InHeight Altura do v�deo (usada para calcular o tamanho do buffer do pipe).
    * @return true se o pipe foi criado e aberto com sucesso, false caso contr�rio.
    */
    bool Create(const FIVR_PipeSettings& Settings, const FString& SessionID, int32 InWidth = 0, int32 InHeight = 0);

     /**
     * Tenta conectar-se ao Named Pipe. Este m�todo bloquear� at� que um cliente
     * (e.g., FFmpeg) se conecte ao pipe.
     * Deve ser chamado DEPOIS que o pipe � criado (com Create()) e DEPOIS que o cliente
     * que vai ler/escrever no pipe � lan�ado.
     * @return true se a conex�o foi bem-sucedida, false caso contr�rio.
     */
    bool Connect(); // <-- NOVO M�TODO

    /**
     * Escreve dados no pipe.
     * @param Data Ponteiro para os dados a serem escritos.
     * @param Size O n�mero de bytes a serem escritos.
     * @return O n�mero de bytes realmente escritos, ou -1 em caso de erro.
     */
    int32 Write(const uint8* Data, int32 NumBytes);

    /**
     * Fecha o pipe e libera seus recursos.
     * Para FIFOs POSIX, isso tamb�m remove o arquivo do sistema de arquivos.
     */
    void Close();

    /**
     * Verifica se o pipe est� aberto e v�lido.
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
    int PipeHandle;     // Placeholder for non-Windows platforms
#endif

    FIVR_PipeSettings PipeSettings;

    FString FullPipePath; // Caminho completo para o pipe
    bool bIsCreatedAndConnected; // Indica se o pipe foi criado e est� pronto para uso

    // Desabilitar c�pia e atribui��o
    FIVR_PipeWrapper(const FIVR_PipeWrapper&) = delete;
    FIVR_PipeWrapper& operator=(const FIVR_PipeWrapper&) = delete;
};

