Scriptname MORWHA Hidden

string Function GetVersion() Global Native
bool Function IsReady() Global Native
Actor Function SpawnTestMortal(Actor akAnchor, Race akRace = None, int aiSex = 1, string asName = "Test Mortal") Global Native
Actor Function SpawnRelativeMortal(Actor akAnchor, Actor akDonor, Race akRace = None, int aiSex = -1, string asName = "Relative Mortal") Global Native
Actor Function SpawnOffspringMortal(Actor akAnchor, Actor akParentA, Actor akParentB, Race akRace = None, int aiSex = -1, string asName = "Offspring Mortal") Global Native
bool Function RerollMortal(Actor akTarget, Race akRaceOverride = None, bool abRerollRaceFromDonors = false, int aiSexOverride = -1, string asNameOverride = "") Global Native
Actor Function SpawnMortalJson(Actor akAnchor, string asOptionsJson) Global Native
bool Function UpdateMortalJson(Actor akTarget, string asPatchJson) Global Native
string Function GetMortalHeritageJson(Actor akTarget) Global Native
