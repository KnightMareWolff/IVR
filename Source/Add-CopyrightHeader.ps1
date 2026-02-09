# Requires -Version 5.1 # Para Windows PowerShell 5.1 e superiores (ou PowerShell Core)

# --- CONFIGURAÇÃO ---
# 1. Defina o cabeçalho de copyright desejado.
#    Use @"..."@ (here-string) para strings multilinhas.
#    Certifique-se de que o cabeçalho termine com uma linha em branco ou com a formatação
#    desejada para separá-lo do código.
$CopyrightHeader = @"
// -------------------------------------------------------------------------------
// Copyright 2025 William Wolff. All Rights Reserved.
// This code is property of William Wolff and protected by copywright law.
// Proibited copy or distribution without expressed authorization of the Author.
// -------------------------------------------------------------------------------

"@

# 2. Caminho raiz é o diretório onde este script está sendo executado.
#    Não é necessário ajustar manualmente este caminho.
$RootPath = $PSScriptRoot

# Define cabeçalhos antigos conhecidos a serem detectados e removidos.
# IMPORTANTE: Adapte estas strings para o CONTEÚDO EXATO dos cabeçalhos antigos
# que você deseja remover dos seus arquivos. Inclua quebras de linha ('`n')
# se o cabeçalho for multi-linha.
# A ordem pode importar se um for substring do outro (coloque o mais específico primeiro).
$OldHeadersToDetectAndRemove = @(
    # Exemplo do cabeçalho padrão da Epic que a Unreal cria. Verifique se o texto e `n`s batem com o que você tem.
    "// Fill out your copyright notice in the Description page of Project Settings.`n" +
    "// Copyright Epic Games, Inc. All Rights Reserved.`n`n", # Este é um padrão comum da Epic (com 2 quebras de linha no final)
    "// Fill out your copyright notice in the Description page of Project Settings.`n" # Uma versão mais curta ou sem o copyright da Epic (com 1 quebra de linha)
    
    # Adicione aqui quaisquer versões ANTIGAS do seu próprio cabeçalho "Adapta" que você queira substituir.
    # Exemplo: "// Copyright 2023 Adapta. All Rights Reserved.`n`n",
    # Exemplo: "// Minha Empresa Velha S.A. Todos os direitos reservados.`n`n"
)

# --- VARIÁVEIS DE CONTADOR ---
$FilesFound = 0
$HeadersAddedOrUpdated = 0
$SkippedFiles = 0

# --- FUNÇÕES HELPER ---

function Process-File {
    param (
        [string]$FilePath,
        [string]$NewHeaderContent,
        [array]$OldHeadersPatterns # Lista de cabeçalhos antigos a remover
    )

    Write-Host "Processando: $FilePath"
    $Changed = $false

    # 1. Criar um backup primeiro
    $BackupFilePath = $FilePath + ".bak"
    # Adicionamos -ErrorAction SilentlyContinue para não parar o script se o backup falhar por permissão, etc.
    Copy-Item -Path $FilePath -Destination $BackupFilePath -Force -ErrorAction SilentlyContinue

    # 2. Ler o conteúdo inteiro do arquivo como uma única string (lida com UTF8 com/sem BOM)
    # -Raw lê o arquivo inteiro como uma string única, mantendo as quebras de linha originais.
    $FileContent = Get-Content -Path $FilePath -Raw -Encoding UTF8 -ErrorAction SilentlyContinue

    # 3. Verificar se o arquivo JÁ começa com o cabeçalho correto e NOVO
    if ($FileContent.StartsWith($NewHeaderContent)) {
        Write-Host "  Ignorando (já possui cabeçalho correto e formato): $FilePath"
        return $false # Indica que nenhuma mudança é necessária
    }

    # 4. Limpar o conteúdo removendo cabeçalhos antigos conhecidos
    $cleanedContent = $FileContent
    foreach ($oldHeader in $OldHeadersPatterns) {
        if ($cleanedContent.StartsWith($oldHeader)) {
            $cleanedContent = $cleanedContent.Substring($oldHeader.Length)
            $Changed = $true
            Write-Host "  Removendo cabeçalho antigo detectado em: $FilePath"
            break # Assumimos que há apenas um cabeçalho antigo significativo no início
        }
    }
    
    # Remover quaisquer linhas em branco ou comentários soltos que possam ter ficado no início
    # após a remoção de um cabeçalho antigo, antes do código real.
    # Isso é um heurístico para pegar restos de comentários de cabeçalho.
    while ($cleanedContent.TrimStart().StartsWith("`n") -or $cleanedContent.TrimStart().StartsWith("`r`n") -or $cleanedContent.TrimStart().StartsWith("//") -or $cleanedContent.TrimStart().StartsWith("/*") -or $cleanedContent.TrimStart().StartsWith("*")) {
        if ($cleanedContent.TrimStart().StartsWith("//")) {
            $cleanedContent = ($cleanedContent -split "`r`n" | Select-Object -Skip 1) -join "`r`n"
        } elseif ($cleanedContent.TrimStart().StartsWith("/*")) {
            $endBlock = $cleanedContent.IndexOf("*/")
            if ($endBlock -ne -1) {
                $cleanedContent = $cleanedContent.Substring($endBlock + 2)
            } else {
                # Block comment started but not ended, implies malformed or very long header.
                # In this case, we just remove everything until the next line if it's a comment.
                $cleanedContent = ($cleanedContent -split "`r`n" | Select-Object -Skip 1) -join "`r`n"
            }
        } else {
            $cleanedContent = $cleanedContent.TrimStart("`n", "`r")
        }
        $Changed = $true # Marca como alterado se algo foi removido aqui
        # Evita loop infinito em arquivos malformados ou apenas com comentários
        if ($cleanedContent.Length -eq 0) { break }
    }


    # 5. Prepend o novo cabeçalho ao conteúdo limpo
    $finalContent = $NewHeaderContent + $cleanedContent
    # Marca como alterado mesmo se não removeu um antigo, porque estamos adicionando/garantindo o novo
    $Changed = $true 

    # 6. Escrever de volta para o arquivo com UTF-8 COM BOM.
    # No Windows PowerShell, -Encoding UTF8 já faz isso.
    Set-Content -Path $FilePath -Value $finalContent -Encoding UTF8 -ErrorAction Stop

    Write-Host "  Atualizado/Adicionado cabeçalho e formato a: $FilePath"
    return $true # Indica que uma mudança foi feita
}

# --- EXECUÇÃO PRINCIPAL ---
Write-Host "Iniciando varredura para adicionar/atualizar cabeçalhos de copyright em '$RootPath'..."

# Verifica se o diretório raiz existe
if (-not (Test-Path $RootPath -PathType Container)) {
    Write-Error "Erro: O diretório raiz '$RootPath' não existe. Isso não deveria acontecer. Verifique se o script está sendo executado de um local válido."
    exit 1 # Sai com código de erro
}

# Pega todos os arquivos .h e .cpp recursivamente
# -File garante que apenas arquivos (não diretórios com extensão similar) sejam retornados.
$Files = Get-ChildItem -Path $RootPath -Recurse -Include *.h,*.cpp -File -ErrorAction SilentlyContinue

foreach ($File in $Files) {
    $FilesFound++
    if (Process-File -FilePath $File.FullName -NewHeaderContent $CopyrightHeader -OldHeadersPatterns $OldHeadersToDetectAndRemove) {
        $HeadersAddedOrUpdated++ # Apenas conta se uma mudança foi feita
    } else {
        $SkippedFiles++
    }
}

Write-Host "`n--- Processo Concluído ---"
Write-Host "Arquivos .h e .cpp encontrados: $FilesFound"
Write-Host "Cabeçalhos adicionados/atualizados: $HeadersAddedOrUpdated"
Write-Host "Arquivos ignorados (já possuíam cabeçalho correto): $SkippedFiles"
