#pragma once

#include "DWLanguageUIContract.h"
#include "Input/Reply.h"
#include "Widgets/Input/SButton.h"

namespace DWLanguageUI
{
    FText L(const TSharedRef<FDWLanguageUIContext>& Context, const TCHAR* Chinese, const TCHAR* English);
    FLinearColor Paper();
    FLinearColor Ink();
    FLinearColor Muted();
    FLinearColor Gold();
    const FSlateBrush* PaperBrush();
    const FSlateBrush* InsetBrush();
    const FButtonStyle& ButtonStyle();
    const FEditableTextBoxStyle& NoteStyle();
    TSharedRef<SWidget> Text(const TSharedRef<FDWLanguageUIContext>& Context, const FText& Value,
        int32 Size = 20, FLinearColor Color = FLinearColor::White, bool bWrap = false);
    TSharedRef<SWidget> Button(const TSharedRef<FDWLanguageUIContext>& Context, const FText& Label,
        FOnClicked OnClicked, float MinWidth = 100.f, bool bEnabled = true, bool bSelected = false);
    TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Child, FMargin Padding = FMargin(16.f), bool bSelected = false);
    TSharedRef<SWidget> CloseButton(const TSharedRef<FDWLanguageUIContext>& Context, FOnClicked OnClicked);
    int32 CountCharacters(const FString& Text); // UTF-16 surrogate pairs count as one scalar.
    bool IsValidNote(const FString& Text, int32 MaxCharacters);
}
