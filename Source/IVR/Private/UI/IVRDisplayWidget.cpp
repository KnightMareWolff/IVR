// IVR/Source/IVR/Private/UI/IVRDisplayWidget.cpp

#include "UI/IVRDisplayWidget.h"
// Não precisamos incluir IVRGlobalStatics ou outros que não são diretamente usados aqui.

void UIVRDisplayWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // Se o componente de captura já foi definido (via Blueprint ou em outra parte do C++),
    // inscreva-se no delegate. O SetCaptureComponent já lida com isso, mas é bom ter uma
    // verificação aqui para garantir caso o componente seja setado antes do NativeConstruct.
    if (LinkedCaptureComponent && !LinkedCaptureComponent->OnRealTimeFrameReady.IsBound())
    {
        LinkedCaptureComponent->OnRealTimeFrameReady.AddDynamic(this, &UIVRDisplayWidget::OnRealTimeFrameReadyHandler);
    }
}

void UIVRDisplayWidget::NativeDestruct()
{
    // É CRUCIAL desinscrever-se do delegate para evitar vazamentos de memória e crashes
    // quando o widget é destruído, mas o componente de captura ainda está ativo.
    if (LinkedCaptureComponent)
    {
        LinkedCaptureComponent->OnRealTimeFrameReady.RemoveDynamic(this, &UIVRDisplayWidget::OnRealTimeFrameReadyHandler);
    }
    CurrentLiveTexture = nullptr; // Limpe a referência da textura ao destruir
    Super::NativeDestruct();
}

void UIVRDisplayWidget::SetCaptureComponent(UIVRCaptureComponent* InCaptureComponent)
{
    // Verifica se o componente de captura está mudando.
    if (LinkedCaptureComponent != InCaptureComponent)
    {
        // Primeiro, desinscreva-se do componente antigo, se houver, para evitar inscrições duplicadas
        // ou chamadas de delegate para um objeto inválido.
        if (LinkedCaptureComponent)
        {
            LinkedCaptureComponent->OnRealTimeFrameReady.RemoveDynamic(this, &UIVRDisplayWidget::OnRealTimeFrameReadyHandler);
        }
        
        // Atribui o novo componente.
        LinkedCaptureComponent = InCaptureComponent;
        
        // Se o novo componente é válido, inscreva-se no delegate.
        // A textura CurrentLiveTexture será preenchida pela primeira vez quando o OnRealTimeFrameReadyHandler for chamado.
        if (LinkedCaptureComponent)
        {
            LinkedCaptureComponent->OnRealTimeFrameReady.AddDynamic(this, &UIVRDisplayWidget::OnRealTimeFrameReadyHandler);
        }
        else
        {
            // Se o componente foi desvinculado (passaram um nullptr), limpe a textura.
            CurrentLiveTexture = nullptr;
        }
    }
}

void UIVRDisplayWidget::OnRealTimeFrameReadyHandler(const FIVR_JustRTFrame& FrameData)
{
    // Este método é chamado a cada vez que o UIVRCaptureComponent broadcasting um novo frame.
    // A textura UTexture2D* que precisamos está dentro de FrameData.LiveTexture.
    // Basta atribuí-la à nossa propriedade CurrentLiveTexture.
    CurrentLiveTexture = FrameData.LiveTexture;

    // IMPORTANTE: O UMG detecta automaticamente que a textura para a qual CurrentLiveTexture
    // aponta foi atualizada (pois o UIVRCaptureComponent escreve novos pixels nela) e redesenha o Image.
    // Não é necessário fazer mais nada aqui para que a imagem no UMG se atualize.

    // Se você precisasse exibir metadados do frame (como largura, altura, ou dados de features),
    // você poderia usar FrameData.Width, FrameData.Height, FrameData.Features, etc., para
    // atualizar Text Blocks ou outros elementos da UI aqui.
}