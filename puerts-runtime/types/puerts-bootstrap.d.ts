declare function require(id: string): unknown;
declare const process: {
  chdir(directory: string): void;
  // Optional on purpose. The PuerTS Node backend does provide process.env, but
  // this declaration is the only description of that object the runtime has,
  // and it is loaded during bootstrap: a throw here takes the whole bridge down
  // before it publishes a session, which is not a price worth paying to read
  // one feature flag.
  readonly env?: Readonly<Record<string, string | undefined>>;
};

declare module "puerts" {
  import { Object } from "ue";

  export interface $Ref<T> {
    readonly __mcpRefType: T;
  }

  export function $ref<T>(value?: T): $Ref<T>;
  export function $unref<T>(value: $Ref<T>): T;

  export const argv: {
    getByName(name: string): Object;
  };
}

declare module "ue" {
  import { $Ref } from "puerts";

  export class Object {
    static Load(path: string): Object;
    static Find(path: string, outer?: Object): Object;
    GetName(): string;
    GetPathName(): string;
    GetClass(): Class;
  }

  export class Class extends Object {
    static Load(path: string): Class;
  }

  export class Vector {
    constructor(x?: number, y?: number, z?: number);
    X: number;
    Y: number;
    Z: number;
  }

  export class Rotator {
    constructor(pitch?: number, yaw?: number, roll?: number);
    Pitch: number;
    Yaw: number;
    Roll: number;
  }

  export class Actor extends Object {
    GetActorLabel(): string;
    GetActorLocation(): Vector;
    GetActorRotation(): Rotator;
    SetActorLabel(label: string, markDirty?: boolean): void;
  }

  export class EditorLevelLibrary {
    static GetAllLevelActors_EditorOnly(): Actor[];
    static SpawnActorFromClass_EditorOnly(actorClass: Class, location: Vector, rotation: Rotator): Actor;
    static DestroyActor_EditorOnly(actor: Actor): boolean;
  }

  export class EditorAssetLibrary {
    static ListAssets_EditorOnly(path: string, recursive?: boolean, includeFolder?: boolean): string[];
    static LoadAsset_EditorOnly(path: string): Object;
    static SaveAsset_EditorOnly(path: string, onlyIfDirty?: boolean): boolean;
  }


  export class MCPPuerTSBridgeService extends Object {
    AcceptCommand(requestJson: string): string;
    CompleteCommand(commandId: string, responseJson: string): string;
    PrepareObjectMutation(object: Object, propertyName: string): boolean;
    FinalizeObjectMutation(object: Object): void;
    IsWritablePropertyAllowed(object: Object, propertyName: string): boolean;
    IsFunctionAllowed(qualifiedFunctionName: string): boolean;
    GetLevelActorsJson(): string;
    GetDiagnosticsJson(actorLimit: number): string;
    SetRuntimeReady(toolCount: number): void;
    IsRuntimeReady(): boolean;
    GetRuntimeToolCount(): number;
    FindAssetsJson(path: string, typeFilter: string, nameFilter: string, recursive: boolean, limit: number, outAssetsJson: $Ref<string>, outError: $Ref<string>): boolean;
    DeleteAsset(assetPath: string, confirm: boolean, force: boolean, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    FindLevelActor(nameOrPath: string): Actor;
    FindObjectByPath(objectPath: string): Object;
    ReadObjectPropertyJson(object: Object, propertyName: string, outValueJson: $Ref<string>, outObjectPath: $Ref<string>, outError: $Ref<string>): boolean;
    SetObjectPropertyJson(object: Object, propertyName: string, valueJson: string, outObjectPath: $Ref<string>, outError: $Ref<string>): boolean;
    ReadActorPropertyJson(nameOrPath: string, propertyName: string, outValueJson: $Ref<string>, outActorPath: $Ref<string>, outError: $Ref<string>): boolean;
    SetActorPropertyJson(nameOrPath: string, propertyName: string, valueJson: string, outActorPath: $Ref<string>, outError: $Ref<string>): boolean;
    CallActorFunctionJson(nameOrPath: string, qualifiedFunctionName: string, argumentsJson: string, outResultJson: $Ref<string>, outActorPath: $Ref<string>, outError: $Ref<string>): boolean;
    SpawnActorJson(classPath: string, x: number, y: number, z: number, pitch: number, yaw: number, roll: number, outActorJson: $Ref<string>, outError: $Ref<string>): boolean;
    DeleteLevelActor(nameOrPath: string, confirmed: boolean, outActorPath: $Ref<string>, outError: $Ref<string>): boolean;
    CreateAuroraSkyMaterialJson(assetPath: string, skyActorName: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildBlueprintJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PatchBlueprintGraphJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PatchBlueprintMembersJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildWidgetJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BindWidgetJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectBlueprintJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildBehaviorTreeJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectBehaviorTreeJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectWidgetJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildAnimBlueprintJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PatchAnimBlueprintJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectAnimBlueprintJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectAnimMontageJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectAnimBlendSpaceJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildBlackboardJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectBlackboardJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectEnvQueryJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectNavigationJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    QueryNavigationJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildNavigationJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildSoundCueJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectAudioAssetJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectClothJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildAIPerceptionJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectAIControllerJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectMaterialJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildMaterialInstanceJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildMaterialJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    ImportTextureJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectSceneJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    ApplySceneBatchJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildLightingJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PatchClassDefaultsJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectLevelSequenceJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildLevelSequenceJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    RenderLevelSequenceJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    ControlJobJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    ConfigureProjectMapsJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PackageProjectJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    InspectInputMappingsJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PatchInputMappingsJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    SetFolderVisibilityJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    PlayCameraShakeJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    QueryPIEAgentJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    ControlPIEAgentJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    BuildPhysicsSceneJson(specJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    ObservePhysicsSceneJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    CaptureViewportJson(requestJson: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    SaveProjectAsset(assetPath: string, outError: $Ref<string>): boolean;
    SaveCurrentLevel(assetPath: string, outSavedPath: $Ref<string>): boolean;
    CreateLevelJson(levelPath: string, templatePath: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    LoadLevelJson(levelPath: string, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    SaveLevelJson(saveAll: boolean, outResultJson: $Ref<string>, outError: $Ref<string>): boolean;
    StartPlayInEditor(outError: $Ref<string>): boolean;
    StopPlayInEditor(outError: $Ref<string>): boolean;
    UndoLastMCPTransaction(expectedId: string, outId: $Ref<string>, outError: $Ref<string>): boolean;
    GetProjectRoot(): string;
    GetRecentLogs(maximumLines: number): string;
    AreShellCommandsAllowed(): boolean;
    GetPipeName(): string;
    GetBearerToken(): string;
    GetMaximumRequestBytes(): number;
    GetRequestTimeoutMilliseconds(): number;
  }
}

declare module "net" {
  export class Socket {
    setEncoding(encoding: "utf8"): this;
    setTimeout(milliseconds: number, callback: () => void): this;
    on(event: "data", listener: (chunk: string) => void): this;
    on(event: "error", listener: (error: Error) => void): this;
    write(data: string): boolean;
    end(data?: string): void;
    destroy(error?: Error): void;
  }
  export interface Server {
    listen(path: string, callback?: () => void): Server;
    on(event: "error", listener: (error: Error) => void): Server;
    close(callback?: (error?: Error) => void): Server;
  }
  export function createServer(listener: (socket: Socket) => void): Server;
}
