Blackboard Usage Scanner - UE 5.6 MVP

INSTALL
1. Copy the BlackboardUsageScanner folder into:
   <YourProject>/Plugins/BlackboardUsageScanner/
2. Regenerate/rebuild the project for Unreal Engine 5.6.
3. Enable the plugin if it is not enabled automatically.
4. Open the Editor console and run:
   BB.ScanUsage

OUTPUT
<Project>/Saved/BlackboardUsageScanner/BlackboardKeySummary.csv
<Project>/Saved/BlackboardUsageScanner/BlackboardKeyReferences.csv

SCOPE
- Scans assets under /Game only.
- Enumerates local keys defined by every UBlackboardData under /Game.
- Traverses Behavior Trees, including:
  - Root decorators
  - Composite nodes
  - Composite services
  - Child decorators
  - Tasks
  - Task services
- Recursively scans reflected properties for FBlackboardKeySelector,
  including selectors nested in USTRUCTs and TArrays.
- Resolves inherited keys to the Blackboard asset that actually defines
  the key.
- Records exact count + BT/node/property location.
- Reports stale selector names as UNRESOLVED_KEY.

IMPORTANT MVP LIMITATION
NO_EXACT_REF does NOT mean the key is unused.
This MVP does NOT yet scan:
- UBlackboardComponent raw FName/FString calls in Blueprint graphs
- literal Name/String pins
- dynamic key-name data flow
- TMap/TSet containers containing FBlackboardKeySelector
