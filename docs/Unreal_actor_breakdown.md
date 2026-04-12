# Das Unreal-Engine-Actor-System in eigener C++ Engine nachbauen

Unreal Engines Actor-System ist eines der ausgereiftesten Gameplay-Objektmodelle der Spieleentwicklung. Es kombiniert ein reflektionsbasiertes Objektsystem (UObject), hierarchische Komponentenarchitektur, automatische Speicherverwaltung via Garbage Collection, vollständige Editor-Integration und ein flexibles Tick-System zu einem kohärenten Ganzen. Dieser Leitfaden zerlegt jedes Subsystem in seine Kernmechanismen und liefert konkrete Datenstrukturen, Algorithmen und Code-Muster für den Nachbau in einer eigenen C++ Engine. Die Empfehlungen orientieren sich an den Design-Entscheidungen von Epic, benennen aber auch Stellen, an denen ein schlankerer Ansatz sinnvoller ist.

---

## 1. Datenhaltung und Memory-Layout

### 1.1 Das Objektmodell: UObject, Reflektion und Property-System

Das Fundament des gesamten Systems ist das **UObject-Basisobjekt**. Jedes Gameplay-Objekt – Actors, Components, Assets, sogar Klassen-Metadaten selbst – erbt von `UObjectBase`. Die Identität eines Objekts wird durch drei Felder definiert: `ClassPrivate` (Laufzeittyp als `UClass*`), `NamePrivate` (ein `FName`) und `OuterPrivate` (das übergeordnete Objekt, das den Besitz-Pfad bildet). Dazu kommen `ObjectFlags` (Zustandsflags wie `RF_Transactional`, `RF_ClassDefaultObject`) und ein `InternalIndex`, der das Objekt im globalen Array adressiert.

Das ungefähre Memory-Layout von `UObjectBase` auf x64 sieht so aus:

```
Offset  Feld                    Größe
+0x00   vfptr (vtable)          8 Byte
+0x08   EObjectFlags            4 Byte
+0x0C   int32 InternalIndex     4 Byte
+0x10   UClass* ClassPrivate    8 Byte
+0x18   FName NamePrivate       8 Byte
+0x20   UObject* OuterPrivate   8 Byte
─── Gesamt: ~0x28 Byte ───
```

**Für eine eigene Engine** genügt ein schlankes Basisobjekt mit vier Kernfeldern: Typ-ID, Name-ID, Owner-Pointer und ein Index ins globale Array. Flags lassen sich als `uint32` Bitfeld realisieren. Entscheidend ist, dass *jedes* verwaltete Objekt diese Identitätsfelder besitzt – sie ermöglichen Serialisierung, Editor-Darstellung und GC gleichermaßen.

#### Das Reflektionssystem

UE erzeugt Reflektionsdaten zur Compile-Zeit über das **Unreal Header Tool (UHT)**. Makros wie `UCLASS()`, `UPROPERTY()` und `UFUNCTION()` annotieren Klassen und Properties. UHT generiert daraus `*.generated.h`-Dateien mit Registrierungscode: `StaticClass()`-Funktionen, `Z_Construct_UClass_*`-Factories und Property-Offset-Berechnungen. Beim Engine-Start durchläuft `ProcessNewlyLoadedUObjects()` alle registrierten Typen und erzeugt deren `UClass`-Instanzen samt Property-Ketten.

**In einer eigenen Engine** hat man drei Optionen für Reflektion:

- **Code-Generator** (analog zu UHT): Ein Prä-Build-Tool parst Header und generiert Registrierungscode. Aufwändig, aber maximal performant.
- **Makro-basierte Registrierung**: C++-Makros, die `offsetof()` und Template-Metaprogrammierung nutzen, um Properties zur Compile-Zeit zu registrieren. Pragmatischer Mittelweg.
- **Externe Beschreibungsdateien** (JSON/XML): Flexibel, aber erfordert Synchronisation mit dem C++-Code.

Die UE-Klassenhierarchie für Typen ist: `UStruct` (Basistyp mit `SuperStruct`, `ChildProperties`-Linked-List, `PropertiesSize`) → `UClass` (erweitert um `ClassFlags`, `ClassCastFlags` für O(1)-Typprüfungen, CDO-Pointer, `FuncMap`) → `UScriptStruct` für Werttypen → `UFunction` für Methoden. Die **`ClassCastFlags`** sind besonders elegant: jede Klasse hat eine Bitmaske ihrer Basisklassen, sodass `IsA()`-Prüfungen ohne String-Vergleiche auskommen.

#### Das Property-System (FProperty)

Seit UE5 sind Properties keine UObjects mehr, sondern leichtgewichtige `FField`-Knoten. Jedes `FProperty` speichert einen **Byte-Offset** (`Offset_Internal`), die **Elementgröße**, die **Array-Dimension** und **Property-Flags**. Subklassen wie `FObjectProperty`, `FStructProperty`, `FArrayProperty` oder `FBoolProperty` beschreiben den konkreten Typ.

| FProperty-Typ | C++-Typ | Speicher am Offset |
|---|---|---|
| `FBoolProperty` | Bitmask oder `bool` | 1 Byte |
| `FIntProperty` | `int32` | 4 Byte |
| `FFloatProperty` | `float` | 4 Byte |
| `FObjectProperty` | `UObject*` | 8 Byte |
| `FStructProperty` | Inline-Bytes des Structs | `PropertiesSize` |
| `FArrayProperty` | `TArray`-Header | 16 Byte |
| `FStrProperty` | `TArray<TCHAR>`-Header | 16 Byte |

`FBoolProperty` verdient besondere Aufmerksamkeit: Es speichert `ByteOffset`, `ByteMask` und `FieldMask`. Wenn `FieldMask == 0xFF`, handelt es sich um ein natives `bool`; andernfalls werden mehrere Bools in ein Byte gepackt – bis zu 8 Booleans pro Byte mit unterschiedlichen Masken. **Für eine eigene Engine** ist dieses Bool-Packing ein lohnenswertes Muster für speichereffiziente Konfigurationen.

#### Class Default Objects (CDO)

Jede `UClass` besitzt ein **Class Default Object** – eine vollständig initialisierte Default-Instanz, die als Template für neue Objekte dient. CDOs werden beim Engine-Start via `UClass::CreateDefaultObject()` erzeugt und erhalten die Flags `RF_ClassDefaultObject | RF_ArchetypeObject`. Beim Spawnen neuer Objekte kopiert die Engine Property-Werte vom CDO (oder einem expliziten Template). Dieses Muster ist hocheffizient: statt jedes Feld einzeln zu initialisieren, wird ein `memcpy`-artiger Bulk-Copy vom CDO durchgeführt, gefolgt von gezielten Übersteuerungen.

**Design-Empfehlung**: Ein CDO-System in der eigenen Engine ist einfach implementierbar und bietet drei Vorteile: (1) schnelle Objekterzeugung via Template-Copy, (2) eine definierte „Ausgangsbasis" für Serialisierung (nur Abweichungen vom CDO müssen gespeichert werden), (3) eine Instanz pro Typ, die der Editor als Defaults-Vorlage anzeigen kann.

---

### 1.2 Globales Objekt-Array (GUObjectArray)

Alle lebenden UObjects sind in einem globalen Array registriert: **`FUObjectArray GUObjectArray`**. Intern nutzt es eine **Chunked-Architektur** (`FChunkedFixedUObjectArray`) mit **65.536 Slots pro Chunk**. Der Zugriff erfolgt über:

```
ChunkIndex  = ObjectIndex / 65536
WithinChunk = ObjectIndex % 65536
Slot        = Chunks[ChunkIndex][WithinChunk]
```

Jeder Slot ist ein `FUObjectItem` mit folgender Struktur:

```cpp
struct FUObjectItem {
    UObjectBase* Object;       // 8 Byte – Pointer auf das Objekt
    int32 Flags;               // 4 Byte – EInternalObjectFlags
    int32 ClusterRootIndex;    // 4 Byte – GC-Cluster-Wurzel
    int32 SerialNumber;        // 4 Byte – Generationszähler für Weak-Pointer
};  // ~20 Byte pro Slot
```

Das Array ist in **zwei Regionen** geteilt: Objekte unterhalb von `ObjFirstGCIndex` sind permanent (CDOs, Typ-Metadaten, Engine-Singletons) und werden vom GC ignoriert – die sogenannte **„Disregard for GC"-Optimierung**. Freigewordene Slots landen in einer `ObjAvailableList` zur Wiederverwendung. Der `SerialNumber` wird bei jeder Slot-Neubelegung inkrementiert, sodass `TWeakObjectPtr` über Index+Serial veraltete Referenzen erkennen kann.

**Für die eigene Engine**: Ein chunked globales Objekt-Array ist der effizienteste Ansatz. Vorteile: O(1)-Zugriff per Index, keine Reallokation des Gesamtarrays, effiziente Iteration über alle Objekte (für GC und Editor-Queries), und die Index+Generation-Kombination ermöglicht sichere Weak-Handles. Eine typische Implementierung:

```cpp
struct ObjectHandle { uint32_t index; uint32_t generation; };
struct ObjectSlot  { Object* ptr; uint32_t generation; uint32_t flags; };
class ObjectArray  {
    static constexpr int CHUNK_SIZE = 65536;
    ObjectSlot** chunks;
    FreeList availableSlots;
};
```

---

### 1.3 Garbage Collection: Mark-and-Sweep mit Token-Stream

UEs GC ist ein **Mark-and-Sweep-Kollektor**, der auf dem Game-Thread läuft (typisch alle 30–60 Sekunden). Die Phasen:

1. **MarkUnreachable** – Alle GC-verwalteten Objekte werden initial als unerreichbar markiert.
2. **Reachability Analysis** – Ausgehend von Root-Objekten werden Referenzen traversiert und erreichbare Objekte markiert.
3. **Gather Unreachable** – Noch unerreichbare Objekte werden gesammelt.
4. **Unhash/Destroy** – `BeginDestroy()` wird gerufen; Objekte werden aus Hash-Tabellen entfernt.
5. **Incremental Purge** – Zeitgeschnittene Destruktion über mehrere Frames, um Stalls zu vermeiden.

Der Clou ist die **Referenz-Erkennung via Token-Stream**. Für jede `UClass` wird einmalig ein kompakter Token-Stream assembliert, der alle UPROPERTY-Referenzpfade beschreibt. Tokens kodieren den Referenztyp (`GCRT_Object` für einzelne `UObject*`, `GCRT_ArrayObject` für `TArray<UObject*>`, `GCRT_ArrayStruct` für Arrays von Structs mit Referenzen etc.) zusammen mit dem Byte-Offset der Property. Der `TFastReferenceCollector` traversiert diesen Stream ohne volle Serialisierung – das ist wesentlich cache-freundlicher als Reflection-basiertes Durchlaufen.

**GC-Cluster** gruppieren Objekte, die zusammen leben und sterben (z.B. ein Asset mit seinen Subobjects). Nur der Cluster-Root wird während der Reachability-Analyse geprüft. Performance-Daten von Epic: **ohne Cluster** dauert die Reachability-Analyse ~17,5 ms bei ~600K Objekten; **mit Clustern** nur ~0,85 ms – eine Reduktion um **95%**.

Für **Nicht-UPROPERTY-Referenzen** bietet UE zwei Muster:
- `AddReferencedObjects()` – Überschreibbare statische Methode auf UObjects, die dem Kollektor manuell Referenzen meldet.
- `FGCObject` – Für Nicht-UObject-Klassen, die UObject-Pointer halten. Registriert sich bei einem globalen `UGCObjectReferencer`.
- `AddToRoot()` – Setzt das `RootSet`-Flag und verhindert Collection permanent.

**Design-Empfehlung für eigene Engine**: Ein einfacher Mark-and-Sweep-GC mit Property-basiertem Referenz-Tracking ist realisierbar. Der Token-Stream-Ansatz ist optimal, aber ein vereinfachter Ansatz kann Properties zur Registrierungszeit in eine Offset-Liste schreiben und diese beim GC-Scan linear durchlaufen. Cluster-Optimierungen lohnen sich erst bei >100K Objekten.

---

### 1.4 Serialisierung: FArchive und UPackage

UEs Serialisierung basiert auf dem **bidirektionalen `operator<<`-Muster**: Derselbe Code liest und schreibt, je nach Modus des `FArchive`:

```cpp
FArchive& operator<<(FArchive& Ar, FVector& V) {
    return Ar << V.X << V.Y << V.Z;
}
// Beim Schreiben: kopiert V → Stream
// Beim Lesen:    kopiert Stream → V
```

Eine `.uasset`-Datei entspricht einem `UPackage` und hat folgende Struktur:

| Sektion | Inhalt |
|---|---|
| Package File Summary | Magic (`0x9E2A83C1`), Versionen, Offsets zu allen Sektionen |
| Name Map | Alle FNames im Paket, referenziert per Index |
| Import Map | Referenzen auf externe Pakete/Objekte |
| Export Map | Im Paket definierte Objekte (Klasse, Serial-Offset, Flags) |
| Depends Map | Abhängigkeiten zwischen Exports |
| Serialisierte Objektdaten | Property-Daten der Exports |
| Bulk Data | Große Binärdaten (Texturen, Audio) für Lazy Loading |

UE nutzt zwei Serialisierungsmodi: **Tagged Property Serialization (TPS)** schreibt für jedes Property einen `FPropertyTag` (Name, Typ, GUID) gefolgt vom Wert – langsamer, aber versionierungsresistent. **Untagged Property Serialization (UPS)** schreibt Properties sequentiell ohne Tags – schnell, aber bei Strukturänderungen inkompatibel. TPS wird für uncooked Assets verwendet, UPS für gepackte Builds.

**Für die eigene Engine**: Das bidirektionale Archive-Muster ist elegant und reduziert Duplikation drastisch. Ein minimales Archiv-Interface:

```cpp
class Archive {
    bool isLoading;
    virtual void serialize(void* data, size_t size) = 0;
    template<typename T> Archive& operator<<(T& val) {
        serialize(&val, sizeof(T)); return *this;
    }
};
```

Tagged Serialization (Property-Name + Typ + Wert) ist für Editor-Assets empfehlenswert; für Runtime-Daten reicht binäre Serialization mit Versionsnummern.

---

### 1.5 Das FName-System

`FName` ist UEs optimierter String-Typ für Identifikatoren. Intern besteht er aus nur **8 Byte**: einem `ComparisonIndex` (4 Byte, Index in den globalen Name-Pool) und einem `Number` (4 Byte, numerisches Suffix). Im Editor kommt optional ein `DisplayIndex` hinzu (12 Byte gesamt mit `WITH_CASE_PRESERVING_NAME`).

Der **globale Name-Pool** (`FNamePool`) nutzt Sharding und Block-Allokation. Jeder `FNameEntry` speichert einen 2-Byte-Header (Länge + Wide-Flag) gefolgt vom Zeichendaten ohne Null-Terminator. Die Hashfunktion ist **CityHash**. FNames sind **case-insensitive**: `"Foo"` und `"foo"` ergeben denselben `ComparisonIndex`.

Das Suffix-System macht FNames ideal für **eindeutige Objektnamen**: `MyActor` hat Number=0, `MyActor_0` hat Number=1, `MyActor_1` hat Number=2 (intern Number = angezeigter Wert + 1). Vergleiche sind **O(1)** – zwei Integer-Vergleiche statt String-Vergleiche. Konstruktion aus Strings ist O(n) wegen Hash-Berechnung und Pool-Lookup.

**Design-Empfehlung**: Ein Name-System mit globalem String-Pool und Index-basiertem Vergleich ist einer der höchsten ROI-Investitionen für eine Game Engine. Minimale Implementierung: Hash-Map von String→ID, Array von ID→String-Pointer. Suffix-Handling optional, aber wertvoll für automatische Benennung.

---

## 2. Laufzeitverwaltung: Lifecycle, Tick und Spawning

### 2.1 Der vollständige Actor-Lifecycle

Ein Actor durchläuft je nach Entstehungsart einen von drei Pfaden, teilt aber einen gemeinsamen Destruktionspfad.

**Pfad A – Laden von Disk** (Level-Actors):
`UObject-Allokation → Konstruktor → PostLoad() → PreInitializeComponents() → InitializeComponent() (pro Komponente) → PostInitializeComponents() → BeginPlay()`

**Pfad B – Play in Editor (PIE)**: Wie Pfad A, aber Actors werden via `PostDuplicate()` aus der Editor-Welt kopiert statt von Disk geladen.

**Pfad C – Runtime-Spawning** (die detaillierteste Kette):

```
NewObject<T>()               // UObject-Allokation + C++-Konstruktor
  → CreateDefaultSubobject() // Default-Komponenten im Konstruktor
  → PostInitProperties()     // Property-Initialisierung abgeschlossen
  → PostActorCreated()       // Nur bei Spawn (mutual exclusive mit PostLoad)
  → ExecuteConstruction()    // Blueprint Construction Script
  → OnConstruction()         // C++ Construction Hook
  → PreInitializeComponents()
  → InitializeComponent()   // Pro Komponente (wenn bWantsInitializeComponent)
  → PostInitializeComponents()
  → BeginPlay()             // Wenn Welt bereits spielt
```

**`BeginPlay`** wird global über diese Kette ausgelöst: `UWorld::BeginPlay() → AGameModeBase::StartPlay() → AGameStateBase::HandleBeginPlay() → AWorldSettings::NotifyBeginPlay()` – letzteres iteriert alle Actors via `FActorIterator` und ruft `DispatchBeginPlay()`. **Wichtig: Die Reihenfolge von BeginPlay zwischen verschiedenen Actors ist undefiniert.** Innerhalb eines Actors werden Component-BeginPlays *vor* dem Actor-BeginPlay aufgerufen.

**Destruktion** folgt immer demselben Pfad: `AActor::Destroy() → UWorld::DestroyActor()`:
1. `EndPlay(EEndPlayReason::Destroyed)` – Cleanup-Logik
2. Kinder-Actors abkoppeln, `OnDestroyed` Broadcast
3. Alle Komponenten unregistrieren
4. Actor aus `ULevel::Actors` entfernen
5. `MarkAsGarbage()` – für GC markieren
6. GC ruft später `BeginDestroy()` → `FinishDestroy()` → Speicher freigeben

**Für die eigene Engine**: Der zweistufige Destruktionsprozess (sofortiges Entfernen aus der Welt + verzögertes Speicherfreigeben via GC) ist ein bewährtes Muster. Es verhindert Dangling-Pointer-Zugriffe innerhalb desselben Frames. Ohne GC kann ein Frame-verzögertes Löschen via Pending-Kill-Queue denselben Effekt erzielen.

---

### 2.2 Component-Lifecycle und Speicherung auf Actors

Actors speichern ihre Komponenten in mehreren Containern:

- **`TSet<UActorComponent*> OwnedComponents`** – Autoritative Menge aller Komponenten
- **`TArray<UActorComponent*> InstanceComponents`** – Laufzeit-hinzugefügte Komponenten (für Serialisierung)
- **`USceneComponent* RootComponent`** – Wurzel der Szenen-Hierarchie

Komponenten werden auf zwei Wegen erzeugt: Im Konstruktor via **`CreateDefaultSubobject<T>()`** (nur dort gültig, erzeugt CDO-Komponenten), oder zur Laufzeit via **`NewObject<T>()` + `RegisterComponent()`**. `RegisterComponent()` ist der zentrale Aktivierungsschritt – er erzeugt Render-/Physik-State, ruft `OnRegister()`, und wenn der Actor bereits initialisiert ist, auch `InitializeComponent()` und `BeginPlay()`.

Die Attachment-Hierarchie von `USceneComponent`s bildet einen Baum. **Parent-Components werden automatisch zu Tick-Prerequisites ihrer Kinder** – der Parent tickt vor dem Child. `SetupAttachment()` wird im Konstruktor verwendet, `AttachToComponent()` zur Laufzeit.

**Design-Empfehlung**: Die Trennung zwischen `OwnedComponents` (authoritativ) und `InstanceComponents` (serialisierbar) ist clever. Für eine eigene Engine genügt initial ein einzelnes `std::vector<Component*>` pro Actor, ergänzt um ein `RootComponent`-Pointer für die Szenen-Hierarchie.

---

### 2.3 World- und Level-Verwaltung

**`UWorld`** ist das Top-Level-Objekt einer Spielwelt. Kernfelder: `PersistentLevel` (immer geladen), `StreamingLevels` (dynamisch geladene Sub-Level), `AuthorityGameMode`, `GameState`. Die Erzeugung folgt der Kette:

```
UWorld::CreateWorld()
  → NewObject<UWorld>()
  → InitializeNewWorld()
    → PersistentLevel = NewObject<ULevel>()
    → Spawn AWorldSettings
  → InitWorld()
    → CreatePhysicsScene()
    → AllocateScene() (Renderer)
    → CreateAISystem()
```

**`ULevel`** speichert Actors in einem schlichten `TArray<AActor*> Actors`. Beim Spawnen wird der Actor diesem Array hinzugefügt, beim Zerstören entfernt. Jedes Level hat ein `UWorld` als Outer.

**World Partition (UE5)** ersetzt das alte Level-Streaming: Die Welt besteht aus einem einzigen Persistent Level. Actors werden im **One File Per Actor (OFPA)**-Format gespeichert – jeder Actor als eigenes `.uasset`. Zur Cook-Zeit werden Actors automatisch in ein Streaming-Grid aufgeteilt; zur Laufzeit streamen Zellen distanzbasiert.

**Für die eigene Engine**: Eine `World`-Klasse, die ein oder mehrere `Level`-Objekte besitzt, wobei jedes Level ein Array von Actor-Pointern hält, ist der bewährte Startpunkt. Level-Streaming kann als Lazy-Loading von Level-Dateien implementiert werden, wobei geladene Actors die reguläre Lifecycle-Kette durchlaufen.

---

### 2.4 Das Tick-System: FTickFunction und Tick-Graph

Das Tick-System basiert auf **`FTickFunction`** – einem Struct (nicht UObject), das pro ticking-fähigem Objekt existiert. Actors haben `PrimaryActorTick` (Typ `FActorTickFunction`), Components haben `PrimaryComponentTick` (Typ `FComponentTickFunction`).

Kernfelder von `FTickFunction`:

| Feld | Typ | Default | Beschreibung |
|---|---|---|---|
| `bCanEverTick` | bool | false | Master-Schalter |
| `TickGroup` | ETickingGroup | varies | Grobe Phase |
| `TickInterval` | float | 0.0 | Min. Sekunden zwischen Ticks |
| `bRunOnAnyThread` | bool | false | Paralleles Ticking erlauben |
| `bHighPriority` | bool | false | Innerhalb der Gruppe priorisiert |

Die **vier Tick-Gruppen** definieren die Ausführungsreihenfolge relativ zur Physik:

| Reihenfolge | Gruppe | Beschreibung |
|---|---|---|
| 1 | **TG_PrePhysics** | Vor der Physiksimulation. Standard für Actors. |
| 2 | **TG_DuringPhysics** | Physik läuft parallel. Standard für Components. |
| 3 | **TG_PostPhysics** | Physik abgeschlossen. Gut für Traces/Queries. |
| 4 | **TG_PostUpdateWork** | Nach allem. Kamera finalisiert. |

**Tick-Dependencies** bilden einen **Directed Acyclic Graph (DAG)**: `AddTickPrerequisiteActor()` und `AddTickPrerequisiteComponent()` erzeugen Kanten „Ich muss nach dem Prerequisite ticken". Der Engine topologisch sortiert diesen DAG pro Tick-Gruppe. **Prerequisites können Tick-Gruppen überschreiben** – wenn ein Prerequisite in einer späteren Gruppe liegt, wartet der Abhängige.

Tick-Registrierung erfolgt während `BeginPlay()` via `RegisterComponentTickFunctions()`. Der `FTickTaskManager` verwaltet alle registrierten Funktionen pro Level (`FTickTaskLevel`). Pro Frame und Tick-Gruppe: (1) Funktionen sammeln, (2) nach Priorität und Prerequisites sortieren, (3) High-Priority zuerst ausführen, (4) Async-Ticks an Worker-Threads verteilen, (5) `TG_NewlySpawned` für neu erzeugte Actors abarbeiten.

**Für die eigene Engine**: Ein Tick-System braucht mindestens: (1) ein `TickFunction`-Struct mit Enable-Flag, Gruppe und Intervall, (2) eine Registry aller aktiven Tick-Funktionen, (3) Gruppierung in Phasen relativ zur Physik, (4) optional einen Dependency-Graph mit topologischer Sortierung. Der DAG-Ansatz ist mächtig, aber für den Start reicht eine priorisierte Liste pro Gruppe.

---

### 2.5 Spawning-Internals und Deferred Spawning

`UWorld::SpawnActor<T>()` akzeptiert `FActorSpawnParameters` mit Feldern wie `Owner`, `Instigator`, `Template`, `OverrideLevel`, `SpawnCollisionHandlingOverride` und `bDeferConstruction`. Der interne Ablauf:

1. Klasse validieren (muss von AActor erben, darf nicht abstrakt sein)
2. Kollisionsbehandlung prüfen
3. `NewObject<AActor>()` im Ziel-Level erzeugen
4. Transform setzen
5. `PostSpawnInitialize()`: Owner/Instigator setzen, Komponenten registrieren
6. Wenn nicht deferred: `ExecuteConstruction()` → `OnConstruction()`
7. `PreInitializeComponents()` → `InitializeComponent()` pro Komponente → `PostInitializeComponents()`
8. In `ULevel::Actors` eintragen
9. `OnActorSpawned` Broadcast
10. Wenn Welt spielt: `DispatchBeginPlay()`

**Deferred Spawning** (`SpawnActorDeferred<T>()`) unterbricht nach Schritt 5 und überspringt die Construction-Script-Ausführung. Der Aufrufer kann Properties setzen, die vor dem Construction Script verfügbar sein müssen. `FinishSpawning()` setzt den Prozess ab Schritt 6 fort.

**Design-Empfehlung**: Deferred Spawning ist ein wertvolles Muster für Systeme, in denen Objekte vor ihrer vollen Aktivierung konfiguriert werden müssen (z.B. Netzwerk-Replikation, Datenbank-gesteuerte Spawns). Implementierung: Ein `bFullyInitialized`-Flag am Objekt, das Construction und BeginPlay blockiert bis `FinishSpawning()` aufgerufen wird.

---

### 2.6 Das Game Framework

UEs Game Framework definiert die Rollen im Gameplay-Loop:

**`AGameModeBase`** existiert **nur auf dem Server** und definiert Spielregeln. Sie bestimmt, welche Klassen für Pawn, PlayerController, PlayerState und GameState verwendet werden. Schlüsselfunktionen: `PreLogin()` (Verbindung akzeptieren/ablehnen), `Login()` (PlayerController erzeugen), `PostLogin()` (erster sicherer Punkt für RPCs), `HandleStartingNewPlayer()` (Default-Pawn spawnen).

**`AGameStateBase`** ist **an alle Clients repliziert** und hält spielweiten Zustand (Punktestand, Match-Status). Das Feld `bReplicatedHasBegunPlay` löst clientseitig die BeginPlay-Kette aus.

**`APlayerController`** repräsentiert den menschlichen Spieler – er empfängt Input, besitzt ein HUD und persistiert über Pawn-Wechsel hinweg. Er existiert nur auf dem besitzenden Client und dem Server.

**`APawn`/`ACharacter`** ist die physische Repräsentation. Possession funktioniert über `APlayerController::Possess(APawn*)` → `APawn::PossessedBy(AController*)`. ACharacter erweitert APawn um `UCharacterMovementComponent`, `UCapsuleComponent` und `USkeletalMeshComponent`.

**`APlayerState`** wird an **alle Clients repliziert** (anders als PlayerController) und speichert spielerübergreifend sichtbare Daten (Name, Score, Team).

**Für die eigene Engine**: Diese Rollen-Trennung (Mode→Rules, State→Data, Controller→Input, Pawn→Body) ist ein bewährtes Architekturmuster unabhängig von UE. Selbst ohne Netzwerk profitiert man von der klaren Verantwortungstrennung. Die Server-Only-Natur des GameMode verhindert Cheating; der replizierte GameState synchronisiert alle Clients.

---

## 3. ECS-Vergleich und Massensimulation

### 3.1 Wo das klassische OOP/Component-Modell an Grenzen stößt

Das Actor+Component-Modell ist intuitiv und flexibel, skaliert aber schlecht bei hohen Entitätszahlen. Benchmarks zeigen: **10.000 individuell tickende Actors kosten ~12,87 ms/Frame**. Ein externer Tick-Manager reduziert dies auf ~4,7 ms; datenorientierte Batch-Verarbeitung auf **~3,6 ms – eine Reduktion um 72%**. Die Ursachen sind dreifach: (1) Virtual-Function-Overhead durch vtable-Lookups pro Tick, (2) Cache-Misses durch verstreute Heap-Allokationen – jeder Actor und jede Component liegt an einer anderen Speicheradresse, (3) ~36% Overhead im Tick-Management-System selbst bei Einzelverarbeitung.

### 3.2 Archetype-basiertes ECS: Mass Entity Framework

UEs **Mass Entity Framework** ist ein Archetype-basiertes ECS, architektonisch verwandt mit Unity DOTS und Flecs. Die Terminologie-Zuordnung:

| Standard-ECS | Mass Entity |
|---|---|
| Entity | Entity (`FMassEntityHandle`) |
| Component | **Fragment** (`FMassFragment`) |
| System | **Processor** (`UMassProcessor`) |

**Fragmente** sind reine Daten-Structs ohne Logik:
```cpp
USTRUCT()
struct FVelocityFragment : public FMassFragment {
    GENERATED_BODY()
    FVector Velocity;
};
```

**Tags** (`FMassTag`) sind Null-Byte-Structs, die ausschließlich zum Filtern dienen – ihre An-/Abwesenheit wird via Bitset verwaltet. **Shared Fragments** teilen Daten über alle Entities eines Archetyps (z.B. LOD-Konfigurationen). **Chunk Fragments** assoziieren Daten mit einem Memory-Chunk statt einzelnen Entities.

#### Archetype-Storage und Memory-Layout

Ein **Archetype** ist eine eindeutige Kombination von Fragment-Typen und Tags. Alle Entities mit derselben Zusammensetzung gehören zum selben Archetype. `FMassArchetypeData` speichert Entities in **Chunks** mit pseudo-SoA-Layout:

```
Chunk 0: [VelFrag₀, VelFrag₁, VelFrag₂] [TransFrag₀, TransFrag₁, TransFrag₂]
Chunk 1: [VelFrag₃, VelFrag₄, VelFrag₅] [TransFrag₃, TransFrag₄, TransFrag₅]
```

Innerhalb jedes Chunks sind die Daten eines Fragment-Typs **kontiguierlich** – verschiedene Fragment-Typen folgen sequentiell, jeweils Cache-Line-aligned. Die **Chunk-Größe** ist auf **128 Byte × 1024 Cache-Lines ≈ 128 KB** dimensioniert, optimiert für moderne CPU-Caches.

**Warum Chunking statt reines SoA?** Reines SoA ist optimal, wenn ein System nur einen Fragment-Typ liest. Sobald ein Processor mehrere Fragments pro Entity benötigt (der Normalfall), liegen diese bei reinem SoA potentiell weit auseinander im Speicher. Chunks halten mehrere Fragment-Arrays nahe beieinander, sodass **ganze Entities in den Cache passen**.

#### Processors und Queries

**`UMassProcessor`** sind zustandslose Klassen mit Verarbeitungslogik. Sie werden **Processing-Phasen** zugeordnet (PrePhysics, DuringPhysics, PostPhysics etc.) und bilden einen **Dependency-Graph** für korrekte Ausführungsreihenfolge. **Multithreading ist Standard** – nur explizit markierte Processors laufen auf dem Game-Thread.

**`FMassEntityQuery`** definiert, welche Fragments ein Processor benötigt (ReadOnly/ReadWrite) und welche vorhanden/absent sein müssen. Die Iteration erfolgt via `ForEachEntityChunk()` über alle passenden Archetype-Chunks.

**Strukturelle Änderungen** (Fragments hinzufügen/entfernen) werden via `FMassCommandBuffer` **deferred** – sie werden am Ende der Verarbeitung ausgeführt, um Iterator-Invalidierung zu vermeiden. Entities, die Fragments hinzufügen/entfernen, werden zwischen Archetypen verschoben.

---

### 3.3 Die Brücke zwischen OOP und ECS

Mass Entity löst das „wichtige Charaktere brauchen volle Actors, Massen brauchen ECS"-Problem durch ein **LOD-basiertes Representation-System**:

| LOD | Distanz | Repräsentation |
|---|---|---|
| High | 0m | Vollständiger UE-Actor (Skeletal Mesh, Animation, AI) |
| Medium | ~500m | Vereinfachter Actor |
| Low | ~1000m | Instanced Static Mesh (kein Actor-Overhead) |
| Off | >5000m | Keine visuelle Repräsentation, reine ECS-Daten |

`UMassAgentComponent` auf einem Actor erzeugt eine korrespondierende Mass-Entity und synchronisiert Daten bidirektional. Das **`FMassRepresentationSubsystem`** verwaltet die visuelle Darstellung und die Actor-Pool-Verwaltung (Spawning/Despawning bei LOD-Wechseln). Hysterese-Schwellen (`BufferHysteresisOnDistancePercentage`) verhindern schnelles Hin- und Herwechseln.

Dieses System wurde in der **Matrix Awakens Demo** eingesetzt: MetaHumans als Mass-kontrollierte Actors für Nahaufnahmen, vertex-animierte Static Meshes für Massen, ZoneGraph für Navigation, StateTree für Verhalten – alles unified über Mass Entity.

---

### 3.4 Data-Oriented Design: SoA, AoS und Cache-Effizienz

Die Performance-Unterschiede zwischen Speicherlayouts sind erheblich:

**Array of Structures (AoS)** – jede Entität als zusammenhängendes Struct:
```cpp
struct Entity { float x, y, z; int health; bool alive; };
Entity entities[10000];
```
Gut wenn alle Felder einer Entität gleichzeitig gebraucht werden. Verschwendet Cache, wenn ein System nur ein Feld iteriert.

**Structure of Arrays (SoA)** – separate Arrays pro Feld:
```cpp
struct Entities { float x[10000]; float y[10000]; int health[10000]; };
```
Benchmarks zeigen **2,5–3× Speedup** bei Iteration über einzelne Felder (10M Structs: ~22–42 ms SoA vs. ~64–108 ms AoS). SoA ermöglicht zudem **SIMD-Vektorisierung** und eliminiert Struct-Padding-Verschwendung.

**Archetype-Storage vs. Sparse Sets**: Archetype-basierte Systeme (Mass, DOTS, Flecs) gruppieren Entities mit gleicher Komposition in dichten Tabellen – **exzellent für Iteration, teuer bei Kompositionsänderungen** (Entity muss zwischen Archetypen verschoben werden). Sparse-Set-Systeme (EnTT) speichern jeden Component-Typ in einem eigenen Sparse Set – **O(1) für Add/Remove, aber Indirektion bei Multi-Component-Queries**.

**Empfehlung**: Für eine eigene Engine ist ein **Archetype-basiertes ECS** der beste Kompromiss, wenn Iteration dominiert (was bei Spielen typisch ist). Kompositionsänderungen werden via **Archetype-Graph** gecacht (Transition-Kanten zwischen Archetypen), sodass nach der ersten Änderung O(1)-Lookups möglich sind.

---

### 3.5 Empfehlungen für den Nachbau in der eigenen Engine

Die entscheidende Designfrage ist: **Wann ECS, wann klassisches Component-System?**

**Klassische Actor+Component** für:
- Spielercharaktere, Boss-NPCs, interaktive Schlüsselobjekte
- Entitäten mit komplexer, einzigartiger Logik
- Prototyping und Editor-Integration
- Kleine Entitätszahlen (<1.000) mit heterogenem Verhalten

**ECS** für:
- Massen-Entitäten (Menschenmengen, Projektile, Partikel, Vegetation)
- Homogene Simulation mit wenigen Datenzugriffen pro Entity
- Alles, was von SIMD und Parallelisierung profitiert
- Entitätszahlen >1.000 mit uniformer Verarbeitung

**Der hybride Ansatz** (das Mass-Pattern) ist die pragmatischste Lösung:
1. Wichtige Entitäten → volle Actors mit OOP-Components
2. Massen → reines ECS
3. Bridge-Layer → Entities können zwischen beiden Welten promoten/demoten basierend auf Kamera-Distanz oder Gameplay-Relevanz

Die Kernstrukturen für ein minimales Archetype-ECS:

```cpp
// Entity-ID mit Generation für sichere Handles
struct EntityID { uint32_t index; uint32_t generation; };

// Component-Type-ID via Template-Counter
template<typename T>
ComponentID getTypeID() { static ComponentID id = nextID++; return id; }

// Archetype: identifiziert durch sortierte Bitmaske von Component-IDs
struct Archetype {
    Bitset signature;           // Welche Components enthalten
    std::vector<void*> columns; // Pro Component-Typ ein Array (SoA)
    std::vector<EntityID> entities;
    size_t count;
};

// Archetype-Graph: cached Transitionen
std::unordered_map<Bitset, Archetype*> archetypeMap;
// add_component(entity, C) → lookup archetype mit signature|C
```

**Chunk-Größe**: 16 KB passt gut in L1-Cache. Mass Entity nutzt ~128 KB für moderne CPUs mit größeren Caches. Ein pragmatischer Wert ist **64 KB** – groß genug für effiziente Iteration, klein genug für L1/L2-Residenz.

---

## 4. Editor-Integration

### 4.1 World Outliner: Wie Actors im Editor gelistet werden

Der **Scene Outliner** wird durch das Slate-Widget `SSceneOutliner` implementiert. Es baut einen hierarchischen Baum aus `ISceneOutlinerTreeItem`-Knoten auf:

- **`FActorTreeItem`** – wrapping einen `TWeakObjectPtr<AActor>`, liefert Display-Name, Icon und Kontextmenü
- **`FFolderTreeItem`** / **`FActorFolderTreeItem`** – Organisationsordner (in UE5 durch `UActorFolder`-Objekte persistent)
- **`FComponentTreeItem`** – Komponenten in der Hierarchie-Ansicht
- **`FWorldTreeItem`** / **`FLevelTreeItem`** – Worlds und Levels

Der Outliner populiert sich, indem er via `FActorIterator` alle Actors der aktuellen `UWorld` traversiert. Die Hierarchie ergibt sich aus Folder-Pfaden (`AActor::GetFolderPath()`) und Attachment-Beziehungen. `FSceneOutlinerModule` bietet Factory-Methoden zum Erstellen konfigurierter Outliner-Widgets mit benutzerdefinierten Spalten, Filtern und Hierarchie-Modi.

**Für die eigene Engine**: Ein Outliner braucht drei Dinge: (1) eine Datenquelle (World→Levels→Actors), (2) ein hierarchisches UI-Widget mit Tree-Items, (3) ein Filter-/Suchsystem. Die Baum-Hierarchie kann aus Parent-Child-Beziehungen (Attachment) und organisatorischen Ordnern (String-Pfade auf Actors) aufgebaut werden. Ordner sind rein organisatorisch – sie erzeugen keine Szenen-Hierarchie.

---

### 4.2 Details Panel: Reflektionsbasierte Property-Editoren

Das Details Panel nutzt das Reflektionssystem, um alle `UPROPERTY`-Felder eines selektierten Objekts aufzulisten. `FPropertyEditorModule` erzeugt `IDetailsView`-Widgets, die die Klasse über `FProperty`-Iteration introspektieren. Jedes Property wird als Zeile mit Label und typspezifischem Editor-Widget dargestellt.

**`IDetailCustomization`** erlaubt die Anpassung des gesamten Details Panels für eine Klasse:

```cpp
class FMyCustomization : public IDetailCustomization {
    void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override {
        IDetailCategoryBuilder& Cat = DetailBuilder.EditCategory("MyCategory");
        Cat.AddProperty("MyProperty");
        Cat.AddCustomRow(LOCTEXT("Label", "Custom"))
           .ValueContent()[ SNew(STextBlock).Text(LOCTEXT("Info", "Custom Widget")) ];
    }
};
// Registrierung:
PropertyModule.RegisterCustomClassLayout("MyClass",
    FOnGetDetailCustomizationInstance::CreateStatic(&FMyCustomization::MakeInstance));
```

`IDetailLayoutBuilder` bietet `EditCategory()`, `GetProperty()`, `HideProperty()` und `GetObjectsBeingCustomized()`. `UPROPERTY`-Metadaten steuern die UI direkt: `EditAnywhere` macht Properties editierbar, `Category="Name"` gruppiert sie, `meta=(EditCondition="bFlag")` aktiviert bedingte Editierung (seit UE 4.23 mit vollen C++-Ausdrücken), `meta=(ClampMin, ClampMax)` begrenzt numerische Werte.

**Property-Change-Notifications** folgen dem Muster: `PreEditChange()` → `Modify()` (Undo-Snapshot) → Wertänderung → `PostEditChangeProperty(FPropertyChangedEvent&)`. Der `FPropertyChangedEvent` enthält das geänderte `FProperty*` und den Änderungstyp (`ValueSet`, `ArrayAdd`, `ArrayRemove` etc.).

**Für die eigene Engine**: Ein reflektionsbasiertes Details Panel ist der größte Produktivitätsgewinn eines Editors. Minimale Implementierung: (1) Property-Registry mit Name, Typ, Offset und Metadaten, (2) generische Widget-Factory, die basierend auf dem Typ den passenden Editor erzeugt (Textfeld für Strings, Slider für Floats, Checkbox für Bools, Color Picker für Farben), (3) Change-Notification-Callbacks für reaktive Updates.

---

### 4.3 Property-Type-Customization für eigene Typen

**`IPropertyTypeCustomization`** passt die Darstellung eines spezifischen `USTRUCT`-Typs engine-weit an. Es hat zwei Overrides: `CustomizeHeader()` für die zusammengeklappte Ansicht und `CustomizeChildren()` für die expandierte Ansicht. Die Registrierung erfolgt via `PropertyModule.RegisterCustomPropertyTypeLayout()`.

**`IPropertyHandle`** abstrahiert den Zugriff auf ein Property, auch bei Multi-Selektion. Kernmethoden: `GetValue()`/`SetValue()` (typisiert), `GetChildHandle()` (für Struct-Member), `AsArray()` (für Array-Properties). Bei Multi-Selektion kann `GetValue()` den Status `FPropertyAccess::MultipleValues` zurückgeben.

UE enthält Built-in-Customizations für `FVector` (X/Y/Z-Felder), `FColor`/`FLinearColor` (Color Picker), `FSoftObjectPath` (Asset Picker), `FGameplayTag` (Tag-Dropdown) und viele weitere im `DetailCustomizations`-Modul.

**Design-Empfehlung**: Das zweistufige System – `IDetailCustomization` pro Klasse und `IPropertyTypeCustomization` pro Typ – ist elegant. Für eine eigene Engine reicht initial ein einziges Customization-Interface pro Typ, das über eine Registry aufgelöst wird:

```cpp
class PropertyWidgetFactory {
    std::unordered_map<TypeID, WidgetCreator> registry;
    Widget* createWidget(const PropertyInfo& prop) {
        auto it = registry.find(prop.typeID);
        if (it != registry.end()) return it->second(prop);
        return createDefaultWidget(prop); // Fallback
    }
};
```

---

### 4.4 Actor-Selection und Undo/Redo

#### Selection-System

`USelection` verwaltet die Editor-Selektion. Drei globale Instanzen: `GEditor->GetSelectedActors()`, `GEditor->GetSelectedObjects()`, `GEditor->GetSelectedComponents()`. Die API bietet `Select()`, `Deselect()`, `DeselectAll()`, `ToggleSelect()`, `IsSelected()` und Iterator-Zugriff. **Selektion löst Events aus**: `USelection::SelectionChangedEvent` (statischer Multicast-Delegate) wird bei jeder Änderung gefeuert.

Viewport-Clicks werden über **Hit Proxies** (`HActor`, `HBSPBrushVert` etc.) auf Actors gemappt. `FLevelEditorViewportClient::ProcessClick()` verarbeitet Hit-Proxy-Ergebnisse und ruft `GEditor->SelectActor()`. Selektierte Actors erhalten eine **Selection-Outline** (Post-Process-Effekt) im Viewport.

**Für die eigene Engine**: Ein Selection-System braucht: (1) eine `Selection`-Klasse mit Set-Semantik (Add/Remove/Toggle/Clear), (2) ein Event/Callback-System für Change-Notifications, (3) Hit-Testing im Viewport (Raycasting gegen Render-Geometrie oder dedizierte Collision-Shapes), (4) visuelle Hervorhebung selektierter Objekte (Outline-Shader oder Farb-Override).

#### Viewport-Gizmos und Transform-Manipulation

`FEditorViewportClient` ist die zentrale Viewport-Klasse. Sie verarbeitet Input via `InputKey()`/`InputAxis()` und delegiert Gizmo-Interaktionen via `InputWidgetDelta()` mit Drag-Vektor, Rotation und Scale-Deltas. Diese werden auf Actors über `AActor::EditorApplyTranslation()`, `EditorApplyRotation()` und `EditorApplyScale()` angewendet.

In UE5 bietet das **Interactive Tools Framework (ITF)** modulare Gizmos: `UInteractiveToolsContext` verwaltet Tools und Gizmos, `UInteractiveGizmoManager` steuert aktive Gizmos, `UTransformGizmo` bietet Sub-Gizmos pro Achse/Ebene.

**Custom Editor Modes** (`FEdMode`/`UEdMode`) können eigene Gizmos via `FPrimitiveDrawInterface` (PDI) zeichnen, Clicks via Hit Proxies verarbeiten und Transform-Widget-Interaktionen überschreiben. **Component Visualizers** (`FComponentVisualizer`) zeichnen Editor-Zeit-Visualisierungen für spezifische Component-Typen und unterstützen interaktive Manipulation über Hit Proxies.

**Snapping** quantisiert Deltas: Grid-Snap für Translation, Rotations-Snap in konfigurierbaren Grad-Schritten (5°, 10°, 15°, 45°, 90°), Scale-Snap für Skalierung.

**Für die eigene Engine**: Ein Gizmo-System braucht: (1) einen Gizmo-Renderer, der Achsen/Ebenen/Ringe zeichnet, (2) Hit-Testing pro Gizmo-Element (Achse, Ebene, Ring), (3) Projektion des Maus-Deltas auf die entsprechende Achse/Ebene im Weltkoordinatensystem, (4) Snapping via Quantisierung der Deltas, (5) Integration mit dem Undo-System (Transaction vor jeder Transformation öffnen).

#### Undo/Redo-System

UEs Undo/Redo basiert auf **Transactions**: `FScopedTransaction` ist ein RAII-Wrapper, der im Konstruktor `GEditor->BeginTransaction()` und im Destruktor `EndTransaction()` aufruft. Innerhalb des Scopes muss auf jedem zu modifizierenden Objekt `Modify()` aufgerufen werden – dies serialisiert den aktuellen Zustand in einen `FObjectRecord` innerhalb der aktiven `FTransaction`.

```cpp
{
    const FScopedTransaction Transaction(LOCTEXT("Move", "Actor verschieben"));
    MyActor->Modify();  // Zustand sichern
    MyActor->SetActorLocation(NewLocation);
}  // Transaction endet, Änderung ist rückgängig machbar
```

`UTransBuffer` verwaltet den Undo- und Redo-Stack. `Undo()` deserialisiert den gesnapshotteten Zustand zurück; `Redo()` wendet die Änderung erneut an. Das `RF_Transactional`-Flag markiert Objekte als Transactions-fähig.

**Integration mit dem Property-System**: Wenn das Details Panel ein Property ändert, wird automatisch `PreEditChange()` → `Modify()` → Wertänderung → `PostEditChangeProperty()` durchlaufen, eingerahmt von einer `FScopedTransaction`.

**Für die eigene Engine**: Ein Undo-System braucht: (1) einen Transaction-Stack, (2) einen Snapshot-Mechanismus (Serialisierung des Objektzustands vor der Änderung), (3) RAII-Scoping für automatisches Begin/End. Der Snapshot kann entweder den gesamten Objekt-Zustand speichern (einfacher, mehr Speicher) oder nur die geänderten Properties (Command-Pattern, komplexer, aber speichereffizient). UE nutzt den Full-Snapshot-Ansatz via Serialisierung, was robust und einfach implementierbar ist.

---

## Schlussbetrachtung: Architektonische Kernentscheidungen

Der Nachbau von UEs Actor-System erfordert Entscheidungen an fünf kritischen Stellen, die den Rest der Architektur formen:

**Erstens**, das Reflektionssystem bestimmt alles Weitere. Ohne Reflektion gibt es kein automatisches Details Panel, keine property-basierte Serialisierung, kein GC-Referenz-Tracking. Ein Makro-basierter Ansatz mit `offsetof()` und Template-Registrierung ist der pragmatischste Einstieg – aufwändig genug für volle Funktionalität, ohne den Overhead eines separaten Code-Generators.

**Zweitens**, die Wahl zwischen GC und manueller Speicherverwaltung. GC vereinfacht den Umgang mit zirkulären Referenzen und Lifecycle-Problemen erheblich, erfordert aber sorgfältiges Referenz-Tracking. Ein einfacher Mark-and-Sweep-GC mit Property-basiertem Scanning ist in wenigen hundert Zeilen implementierbar und bietet bereits 80% des Nutzens.

**Drittens**, die Hybrid-Architektur aus OOP-Actors und ECS-Entities. Statt sich für eines zu entscheiden, ist der Brücken-Ansatz – wichtige Entitäten als volle Objekte, Massen als ECS-Daten, LOD-gesteuerte Promotion/Demotion – die bewährteste Strategie in der Industrie.

**Viertens**, das Tick-System mit Dependency-Graph. Die Investition in einen DAG-basierten Tick-Scheduler zahlt sich aus, sobald Systeme wechselseitige Ausführungsreihenfolgen benötigen (Physik vor Animation, Input vor Bewegung). Ohne DAG entstehen schwer debugbare Ordnungsprobleme.

**Fünftens**, das Undo-System muss von Anfang an in die Architektur eingebaut werden. Nachträgliches Hinzufügen ist extrem aufwändig. Der Snapshot-via-Serialisierung-Ansatz nutzt das ohnehin vorhandene Serialisierungssystem und erfordert minimal zusätzlichen Code – aber nur, wenn jedes editierbare Objekt serialisierbar ist. Die Entscheidung, Serialisierung und Undo als *dasselbe* Subsystem zu behandeln, ist eine der elegantesten Design-Entscheidungen der UE-Architektur.