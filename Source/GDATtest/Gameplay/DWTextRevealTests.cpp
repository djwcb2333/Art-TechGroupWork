#if WITH_DEV_AUTOMATION_TESTS
#include "DWTextRevealComponent.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SWidget.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWTextRevealUnicodeTest,"DoughWorld.TextReveal.UnicodeAndPlayback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWTextRevealUnicodeTest::RunTest(const FString&)
{
 UTextBlock* Text=NewObject<UTextBlock>();
 UDWTextRevealComponent* Effect=NewObject<UDWTextRevealComponent>();
 Effect->Initialize(Text);Effect->PreConstruct(false);
 const TSharedRef<SWidget> Wrapper=Effect->RebuildWidgetWithContent(Text->TakeWidget());
 Effect->Construct();
 TestTrue(TEXT("Ordinary TextBlock is supported"),Effect->IsSupportedOwner());
 Effect->PlayText(FText::FromString(TEXT("A\x0301中\r\n\xd83d\xde00 B")));
 TestEqual(TEXT("Combining mark and surrogate pair each remain one grapheme; CRLF excluded"),Effect->GetCharacterCount(),5);
 TestEqual(TEXT("No initial character flashes before its scheduled time"),Effect->GetVisibleCharacterCount(),0);
 Effect->SetPlaybackTime(Effect->InitialDelay+.001f);
 TestEqual(TEXT("Silent seek reveals first complete grapheme"),Effect->GetVisibleCharacterCount(),1);
 TestEqual(TEXT("Seek never plays audio"),Effect->GetBlipsPlayed(),0);
 Effect->Pause();TestTrue(TEXT("Pause recorded"),Effect->IsPaused());Effect->Resume();TestFalse(TEXT("Resume recorded"),Effect->IsPaused());
 Effect->SkipToEnd();TestFalse(TEXT("Skip completes"),Effect->IsPlaying());TestEqual(TEXT("Skip shows all"),Effect->GetVisibleCharacterCount(),5);
 TestFalse(TEXT("Skip leaves no sound"),Effect->IsBlipPlaying());
 Effect->Stop(false);TestEqual(TEXT("Cancel can clear display"),Effect->GetVisibleCharacterCount(),0);
 TestEqual(TEXT("Cancelling never truncates source text"),Text->GetText().ToString(),FString(TEXT("A\x0301中\r\n\xd83d\xde00 B")));
 Effect->Replay();Effect->SetPlaybackTime(.5f);Effect->SetPlaybackTime(0);TestEqual(TEXT("Backward seek hides future characters"),Effect->GetVisibleCharacterCount(),0);
 Effect->PlayText(FText::GetEmpty());TestEqual(TEXT("Empty text has no characters"),Effect->GetCharacterCount(),0);TestEqual(TEXT("Empty text duration zero"),Effect->GetDuration(),0.f);
 Effect->Destruct();TestFalse(TEXT("Destruction stops playback"),Effect->IsPlaying());TestFalse(TEXT("Destruction stops audio"),Effect->IsBlipPlaying());
 UDWTextRevealComponent* Invalid=NewObject<UDWTextRevealComponent>();Invalid->Initialize(NewObject<UButton>());TestFalse(TEXT("Button itself is not supported"),Invalid->IsSupportedOwner());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWTextVoiceSlotsTest,"DoughWorld.TextReveal.ExpandedVoiceSlots",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWTextVoiceSlotsTest::RunTest(const FString&)
{
 auto* P=NewObject<UDWTextVoiceProfile>();
 TestEqual(TEXT("36 explicit English slots"),P->EnglishCharacterSounds.Num(),36);
 TestEqual(TEXT("8 initial Chinese slots"),P->ChineseBlipSounds.Num(),8);
 auto* Fallback=NewObject<USoundWave>();P->FallbackSound=Fallback;
 const FString Keys=TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
 for(TCHAR C:Keys)
 {
  auto* Sound=NewObject<USoundWave>();const FString Key=FString::Chr(C);P->EnglishCharacterSounds.Add(FName(*Key),Sound);
  TestTrue(*FString::Printf(TEXT("Exact slot %s"),*Key),P->ResolveCharacterSound(Key)==Sound);
  TestTrue(*FString::Printf(TEXT("Lowercase slot %s"),*Key),P->ResolveCharacterSound(Key.ToLower())==Sound);
 }
 P->EnglishA=NewObject<USoundWave>();
 TestTrue(TEXT("Exact A overrides old grouped A"),P->ResolveCharacterSound(TEXT("A"))==P->EnglishCharacterSounds[FName(TEXT("A"))].Get());
 P->EnglishCharacterSounds[FName(TEXT("A"))]=nullptr;
 TestTrue(TEXT("Empty A preserves legacy profile"),P->ResolveCharacterSound(TEXT("a"))==P->EnglishA.Get());
 P->EnglishA=nullptr;TestTrue(TEXT("Empty A uses fallback"),P->ResolveCharacterSound(TEXT("a"))==Fallback);
 P->bSpeakNumbers=false;TestNull(TEXT("Numbers off overrides populated digit slot"),P->ResolveCharacterSound(TEXT("7")));P->bSpeakNumbers=true;
 TestNull(TEXT("Punctuation silent"),P->ResolveCharacterSound(TEXT("。")));TestNull(TEXT("Whitespace silent"),P->ResolveCharacterSound(TEXT(" ")));TestNull(TEXT("Empty silent"),P->ResolveCharacterSound(TEXT("")));
 auto* Chinese=NewObject<USoundWave>();P->ChineseBlipSounds[7]=Chinese;
 for(int32 C=0x4e00;C<0x4e40;++C)TestTrue(TEXT("Sparse pool skips empty slots"),P->ResolveCharacterSound(FString::Chr(TCHAR(C)))==Chinese);
 TestTrue(TEXT("Forced Chinese uses Chinese pool for English"),P->ResolveCharacterSound(TEXT("A"),EDWTextVoiceLanguage::Chinese)==Chinese);
 TSet<USoundBase*> Expected;for(int32 I=0;I<12;++I){auto* S=NewObject<USoundWave>();P->ChineseBlipSounds.Add(S);Expected.Add(S);}Expected.Add(Chinese);
 TSet<USoundBase*> Seen;
 for(int32 C=0x4e00;C<0x5000;++C){const FString Char=FString::Chr(TCHAR(C));auto* S=P->ResolveCharacterSound(Char);TestTrue(TEXT("Expanded pool returns assigned sound"),Expected.Contains(S));TestTrue(TEXT("Stable selection"),S==P->ResolveCharacterSound(Char));Seen.Add(S);}
 TestEqual(TEXT("Pool can exceed eight entries"),Seen.Num(),Expected.Num());
 P->ChineseBlipSounds.Empty();TestTrue(TEXT("Empty pool uses fallback"),P->ResolveCharacterSound(TEXT("中"))==Fallback);
 P->FallbackSound=nullptr;TestNull(TEXT("No samples is silent"),P->ResolveCharacterSound(TEXT("中")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWTextRevealLayoutTest,"DoughWorld.TextReveal.LayoutAtDifferentScales",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWTextRevealLayoutTest::RunTest(const FString&)
{
 auto* Text=NewObject<UTextBlock>();Text->SetText(FText::FromString(TEXT("Bread of the Wild")));
 auto Font=Text->GetFont();Font.Size=66;Text->SetFont(Font);
 auto* Effect=NewObject<UDWTextRevealComponent>();Effect->Initialize(Text);Effect->PreConstruct(false);
 const TSharedRef<SWidget> Native=Text->TakeWidget();
 const TSharedRef<SWidget> Wrapper=Effect->RebuildWidgetWithContent(Native);Effect->Construct();
 for(float Scale:{.65f,1.f,1.5f,2.f,.8f})
 {
  Native->Invalidate(EInvalidateWidgetReason::Layout);Native->SlatePrepass(Scale);
  Wrapper->Invalidate(EInvalidateWidgetReason::Layout);Wrapper->SlatePrepass(Scale);
  const double Expected=Native->GetDesiredSize().X,Actual=Wrapper->GetDesiredSize().X;
  TestTrue(*FString::Printf(TEXT("Full title desired width matches native TextBlock at scale %.2f: %.2f vs %.2f"),Scale,Actual,Expected),FMath::Abs(Actual-Expected)<FMath::Max(4.,Expected*.025));
 }
 Effect->Destruct();return true;
}
#endif
