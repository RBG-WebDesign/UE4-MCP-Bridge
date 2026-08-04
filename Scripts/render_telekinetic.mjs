import { renderClass, validateClassSpec } from "../mcp-server/dist/cpp-authoring.js";

const spec = {
  name: "TelekineticComponent",
  module: "BridgeInstallTest",
  parent: "ActorComponent",
  class_specifiers: "ClassGroup=(Custom), meta=(BlueprintSpawnableComponent)",
  properties: [
    { name: "GrabDistance", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Telekinesis", default: "1200.0f" },
    { name: "HoldDistance", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Telekinesis", default: "300.0f" },
    { name: "ImpulseStrength", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Telekinesis", default: "1800.0f" },
    { name: "bIsHolding", type: "bool", specifiers: "VisibleAnywhere, BlueprintReadOnly", category: "Telekinesis", default: "false" },
    { name: "OnStateChanged", type: "FOnTelekineticStateChanged", specifiers: "BlueprintAssignable", category: "Telekinesis" },
  ],
  functions: [
    { name: "GrabTarget", return_type: "bool", params: "UPrimitiveComponent* TargetComponent, FVector TargetLocation", specifiers: "BlueprintCallable, Category = \"Telekinesis\"", body: "bIsHolding = true;\nif (OnStateChanged.IsBound()) OnStateChanged.Broadcast(true);\nreturn true;" },
    { name: "ReleaseTarget", return_type: "void", params: "", specifiers: "BlueprintCallable, Category = \"Telekinesis\"", body: "bIsHolding = false;\nif (OnStateChanged.IsBound()) OnStateChanged.Broadcast(false);" },
    { name: "ThrowTarget", return_type: "void", params: "FVector Direction", specifiers: "BlueprintCallable, Category = \"Telekinesis\"", body: "bIsHolding = false;\nif (OnStateChanged.IsBound()) OnStateChanged.Broadcast(false);" },
    { name: "IsHoldingTarget", return_type: "bool", params: "", specifiers: "BlueprintPure, Category = \"Telekinesis\"", is_const: true, body: "return bIsHolding;" },
  ],
  delegates: [
    { name: "FOnTelekineticStateChanged", params: [{ name: "bHolding", type: "bool" }], specifiers: "BlueprintCallable", comment: "State change delegate" },
  ],
};

const issues = validateClassSpec(spec);
console.log("Issues:", issues);
const res = renderClass(spec);
console.log("--- HEADER ---\n" + res.header);
console.log("\n--- SOURCE ---\n" + res.source);
