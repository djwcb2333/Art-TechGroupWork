// External candidate, no Unreal dependencies. Steps advance on owned completion signals.
#pragma once
#include <cstdint>
namespace DWOpeningSteps
{
enum class Scene { Wake, Reveal, Encounter };
enum class Kind { Dialogue, Camera, Action, Done, Cancelled };
enum class Step { WakeDark, WakeIntro, WakeThought, WakeClose, RevealLook, RevealThought,
                  RevealBite, RevealReturn, EncounterIntro, EncounterSpoken,
                  EncounterTurn, EncounterFace, Done, Cancelled };
enum class Completion { Success, Failed };
struct Token { Step step; std::uint64_t revision; };
class Sequence
{
public:
 explicit Sequence(Scene scene):step(scene==Scene::Wake?Step::WakeDark:scene==Scene::Reveal?Step::RevealLook:Step::EncounterIntro){}
 Step Current()const{return step;}
 Token CurrentToken()const{return {step,revision};}
 Kind CurrentKind()const
 {
  switch(step){
   case Step::WakeDark:case Step::WakeThought:case Step::RevealThought:case Step::EncounterSpoken:return Kind::Dialogue;
   case Step::RevealBite:return Kind::Action;
   case Step::Done:return Kind::Done;case Step::Cancelled:return Kind::Cancelled;
   default:return Kind::Camera;
  }
 }
 bool Complete(Token token,Kind kind,Completion result)
 {
  if(token.step!=step||token.revision!=revision||kind!=CurrentKind()||kind==Kind::Done||kind==Kind::Cancelled)return false;
  ++revision;
  if(result==Completion::Failed){step=Step::Cancelled;return true;}
  switch(step){
   case Step::WakeDark:step=Step::WakeIntro;break;case Step::WakeIntro:step=Step::WakeThought;break;
   case Step::WakeThought:step=Step::WakeClose;break;case Step::WakeClose:step=Step::Done;break;
   case Step::RevealLook:step=Step::RevealThought;break;case Step::RevealThought:step=Step::RevealBite;break;
   case Step::RevealBite:step=Step::RevealReturn;break;case Step::RevealReturn:step=Step::Done;break;
   case Step::EncounterIntro:step=Step::EncounterSpoken;break;case Step::EncounterSpoken:step=Step::EncounterTurn;break;
   case Step::EncounterTurn:step=Step::EncounterFace;break;case Step::EncounterFace:step=Step::Done;break;
   default:return false;
  }
  return true;
 }
 void Cancel(){if(step!=Step::Cancelled&&step!=Step::Done){++revision;step=Step::Cancelled;}}
private:Step step;std::uint64_t revision=1;
};
}
