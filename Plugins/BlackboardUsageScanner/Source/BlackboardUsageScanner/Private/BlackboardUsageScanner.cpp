#include "BlackboardUsageScanner.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlackboardUsageScanner, Log, All);

namespace BlackboardUsageScanner
{
    struct FKeyDefinition
    {
        FString BlackboardPath;
        FString BlackboardName;
        FName KeyName = NAME_None;
        FString KeyType;
        bool bInstanceSynced = false;

        int32 ExactReferenceCount = 0;
        int32 RawLiteralReferenceCount = 0;
    };

    struct FExactUsage
    {
        FString ResolutionStatus;

        FString DefiningBlackboardPath;
        FString ContextBlackboardPath;
        FName KeyName = NAME_None;

        FString BehaviorTreePath;
        FString TreePath;

        FString NodeKind;
        FString NodeName;
        FString NodeClass;
        uint16 ExecutionIndex = 0;

        FString PropertyPath;
    };

    struct FRawUsage
    {
        FString MatchStatus;

        FString BlackboardPath;
        FName KeyName = NAME_None;
        int32 MatchingBlackboardCount = 0;

        FString BlueprintPath;
        FString GraphName;
        FString NodeTitle;
        FString NodeClass;
        FString NodeGuid;

        FString SourceKind;
        FString LiteralValue;
    };

    struct FDynamicAccess
    {
        FString BlueprintPath;
        FString GraphName;
        FString NodeTitle;
        FString NodeClass;
        FString NodeGuid;
        FString Reason;
    };

    static FString MakeKeyId(const FString& BlackboardPath, const FName KeyName)
    {
        return BlackboardPath + TEXT("::") + KeyName.ToString();
    }

    static FString CsvEscape(FString Value)
    {
        Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Value);
    }

    static bool MatchesFilter(
        const UBlackboardData* Blackboard,
        const FString& Filter)
    {
        if (!Blackboard)
        {
            return false;
        }

        if (Filter.IsEmpty())
        {
            return true;
        }

        return Blackboard->GetName().Equals(Filter, ESearchCase::IgnoreCase)
            || Blackboard->GetPathName().Contains(Filter, ESearchCase::IgnoreCase);
    }

    static bool IsBlackboardContextInScope(
        const UBlackboardData* ContextBlackboard,
        const FString& Filter)
    {
        if (Filter.IsEmpty())
        {
            return true;
        }

        for (const UBlackboardData* BB = ContextBlackboard;
             BB;
             BB = BB->Parent)
        {
            if (MatchesFilter(BB, Filter))
            {
                return true;
            }
        }

        return false;
    }

    static FString GetNodeKind(const UBTNode* Node)
    {
        if (!Node)
        {
            return TEXT("Unknown");
        }

        if (Node->IsA<UBTTaskNode>())
        {
            return TEXT("Task");
        }

        if (Node->IsA<UBTDecorator>())
        {
            return TEXT("Decorator");
        }

        if (Node->IsA<UBTService>())
        {
            return TEXT("Service");
        }

        if (Node->IsA<UBTCompositeNode>())
        {
            return TEXT("Composite");
        }

        return TEXT("BTNode");
    }

    static const UBlackboardData* FindDefiningBlackboard(
        const UBlackboardData* ContextBlackboard,
        const FName KeyName)
    {
        for (const UBlackboardData* Blackboard = ContextBlackboard;
             Blackboard;
             Blackboard = Blackboard->Parent)
        {
            for (const FBlackboardEntry& Entry : Blackboard->Keys)
            {
                if (Entry.EntryName == KeyName)
                {
                    return Blackboard;
                }
            }
        }

        return nullptr;
    }

    static TArray<FAssetData> GetAssetsOfClassInGame(const UClass* AssetClass)
    {
        TArray<FAssetData> Assets;

        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
                TEXT("AssetRegistry"));

        FARFilter Filter;
        Filter.ClassPaths.Add(AssetClass->GetClassPathName());
        Filter.PackagePaths.Add(FName(TEXT("/Game")));
        Filter.bRecursiveClasses = true;
        Filter.bRecursivePaths = true;

        AssetRegistryModule.Get().GetAssets(Filter, Assets);
        return Assets;
    }

    // ---------------------------------------------------------------------
    // Blackboard registry
    // ---------------------------------------------------------------------

    static void BuildBlackboardRegistry(
        const FString& BlackboardFilter,
        TMap<FString, FKeyDefinition>& OutKeyDefinitions,
        TMultiMap<FName, FString>& OutKeyNameToIds,
        int32& OutBlackboardCount)
    {
        const TArray<FAssetData> BlackboardAssets =
            GetAssetsOfClassInGame(UBlackboardData::StaticClass());

        OutBlackboardCount = 0;

        for (const FAssetData& AssetData : BlackboardAssets)
        {
            const UBlackboardData* Blackboard =
                Cast<UBlackboardData>(AssetData.GetAsset());

            if (!Blackboard || !MatchesFilter(Blackboard, BlackboardFilter))
            {
                continue;
            }

            ++OutBlackboardCount;

            for (const FBlackboardEntry& Entry : Blackboard->Keys)
            {
                FKeyDefinition Definition;
                Definition.BlackboardPath = Blackboard->GetPathName();
                Definition.BlackboardName = Blackboard->GetName();
                Definition.KeyName = Entry.EntryName;
                Definition.KeyType =
                    Entry.KeyType
                        ? Entry.KeyType->GetClass()->GetName()
                        : TEXT("<None>");
                Definition.bInstanceSynced = Entry.bInstanceSynced != 0;

                const FString KeyId =
                    MakeKeyId(Definition.BlackboardPath, Definition.KeyName);

                OutKeyDefinitions.Add(KeyId, Definition);
                OutKeyNameToIds.Add(Definition.KeyName, KeyId);
            }
        }
    }

    // ---------------------------------------------------------------------
    // Exact FBlackboardKeySelector scan
    // ---------------------------------------------------------------------

    static void AddExactUsage(
        const UBehaviorTree* Tree,
        const UBTNode* Node,
        const FString& TreePath,
        const FString& PropertyPath,
        const FBlackboardKeySelector& Selector,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FExactUsage>& Usages)
    {
        if (!Tree || !Node || Selector.SelectedKeyName.IsNone())
        {
            return;
        }

        FExactUsage Usage;
        Usage.KeyName = Selector.SelectedKeyName;
        Usage.BehaviorTreePath = Tree->GetPathName();
        Usage.ContextBlackboardPath = Tree->BlackboardAsset
            ? Tree->BlackboardAsset->GetPathName()
            : TEXT("");
        Usage.TreePath = TreePath;
        Usage.NodeKind = GetNodeKind(Node);
        Usage.NodeName = Node->GetNodeName();
        Usage.NodeClass = Node->GetClass()->GetPathName();
        Usage.ExecutionIndex = Node->GetExecutionIndex();
        Usage.PropertyPath = PropertyPath;

        const UBlackboardData* DefiningBlackboard =
            FindDefiningBlackboard(
                Tree->BlackboardAsset,
                Selector.SelectedKeyName);

        if (DefiningBlackboard)
        {
            Usage.ResolutionStatus = TEXT("RESOLVED");
            Usage.DefiningBlackboardPath = DefiningBlackboard->GetPathName();

            const FString KeyId =
                MakeKeyId(Usage.DefiningBlackboardPath, Usage.KeyName);

            if (FKeyDefinition* Definition = KeyDefinitions.Find(KeyId))
            {
                ++Definition->ExactReferenceCount;
            }
            else
            {
                Usage.ResolutionStatus = TEXT("RESOLVED_OUTSIDE_SELECTED_SCOPE");
            }
        }
        else
        {
            Usage.ResolutionStatus = TEXT("UNRESOLVED_KEY");
            Usage.DefiningBlackboardPath = TEXT("");
        }

        Usages.Add(MoveTemp(Usage));
    }

    static void ScanPropertyRecursive(
        const FProperty* Property,
        const void* ValuePtr,
        const FString& PropertyPath,
        const UBehaviorTree* Tree,
        const UBTNode* Node,
        const FString& TreePath,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FExactUsage>& Usages)
    {
        if (!Property || !ValuePtr)
        {
            return;
        }

        if (const FStructProperty* StructProperty =
                CastField<FStructProperty>(Property))
        {
            if (StructProperty->Struct == FBlackboardKeySelector::StaticStruct())
            {
                const FBlackboardKeySelector* Selector =
                    static_cast<const FBlackboardKeySelector*>(ValuePtr);

                if (Selector)
                {
                    AddExactUsage(
                        Tree,
                        Node,
                        TreePath,
                        PropertyPath,
                        *Selector,
                        KeyDefinitions,
                        Usages);
                }

                return;
            }

            for (TFieldIterator<FProperty> It(StructProperty->Struct);
                 It;
                 ++It)
            {
                const FProperty* InnerProperty = *It;
                const void* InnerValuePtr =
                    InnerProperty->ContainerPtrToValuePtr<void>(ValuePtr);

                ScanPropertyRecursive(
                    InnerProperty,
                    InnerValuePtr,
                    PropertyPath + TEXT(".") + InnerProperty->GetName(),
                    Tree,
                    Node,
                    TreePath,
                    KeyDefinitions,
                    Usages);
            }

            return;
        }

        if (const FArrayProperty* ArrayProperty =
                CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);

            for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
            {
                const void* ElementPtr = ArrayHelper.GetRawPtr(Index);

                ScanPropertyRecursive(
                    ArrayProperty->Inner,
                    ElementPtr,
                    FString::Printf(TEXT("%s[%d]"), *PropertyPath, Index),
                    Tree,
                    Node,
                    TreePath,
                    KeyDefinitions,
                    Usages);
            }

            return;
        }
    }

    static void ScanNode(
        const UBehaviorTree* Tree,
        const UBTNode* Node,
        const FString& TreePath,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FExactUsage>& Usages)
    {
        if (!Tree || !Node)
        {
            return;
        }

        for (TFieldIterator<FProperty> It(Node->GetClass());
             It;
             ++It)
        {
            const FProperty* Property = *It;
            const void* ValuePtr =
                Property->ContainerPtrToValuePtr<void>(Node);

            ScanPropertyRecursive(
                Property,
                ValuePtr,
                Property->GetName(),
                Tree,
                Node,
                TreePath,
                KeyDefinitions,
                Usages);
        }
    }

    static FString MakeReadableNodeSegment(
        const TCHAR* Kind,
        int32 Index,
        const UBTNode* Node)
    {
        const FString NodeName =
            Node ? Node->GetNodeName() : TEXT("<null>");

        return FString::Printf(
            TEXT("%s[%d](%s)"),
            Kind,
            Index,
            *NodeName);
    }

    static void ScanTask(
        const UBehaviorTree* Tree,
        const UBTTaskNode* Task,
        const FString& TreePath,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FExactUsage>& Usages)
    {
        if (!Task)
        {
            return;
        }

        ScanNode(Tree, Task, TreePath, KeyDefinitions, Usages);

        for (int32 ServiceIndex = 0;
             ServiceIndex < Task->Services.Num();
             ++ServiceIndex)
        {
            const UBTService* Service = Task->Services[ServiceIndex];

            ScanNode(
                Tree,
                Service,
                TreePath
                    + TEXT("/@")
                    + MakeReadableNodeSegment(
                        TEXT("Service"),
                        ServiceIndex,
                        Service),
                KeyDefinitions,
                Usages);
        }
    }

    static void ScanCompositeRecursive(
        const UBehaviorTree* Tree,
        const UBTCompositeNode* Composite,
        const FString& TreePath,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FExactUsage>& Usages)
    {
        if (!Composite)
        {
            return;
        }

        ScanNode(Tree, Composite, TreePath, KeyDefinitions, Usages);

        for (int32 ServiceIndex = 0;
             ServiceIndex < Composite->Services.Num();
             ++ServiceIndex)
        {
            const UBTService* Service = Composite->Services[ServiceIndex];

            ScanNode(
                Tree,
                Service,
                TreePath
                    + TEXT("/@")
                    + MakeReadableNodeSegment(
                        TEXT("Service"),
                        ServiceIndex,
                        Service),
                KeyDefinitions,
                Usages);
        }

        for (int32 ChildIndex = 0;
             ChildIndex < Composite->Children.Num();
             ++ChildIndex)
        {
            const FBTCompositeChild& Child = Composite->Children[ChildIndex];

            const FString ChildBasePath =
                FString::Printf(
                    TEXT("%s/Child[%d]"),
                    *TreePath,
                    ChildIndex);

            for (int32 DecoratorIndex = 0;
                 DecoratorIndex < Child.Decorators.Num();
                 ++DecoratorIndex)
            {
                const UBTDecorator* Decorator =
                    Child.Decorators[DecoratorIndex];

                ScanNode(
                    Tree,
                    Decorator,
                    ChildBasePath
                        + TEXT("/@")
                        + MakeReadableNodeSegment(
                            TEXT("Decorator"),
                            DecoratorIndex,
                            Decorator),
                    KeyDefinitions,
                    Usages);
            }

            if (Child.ChildTask)
            {
                ScanTask(
                    Tree,
                    Child.ChildTask,
                    ChildBasePath
                        + TEXT("/")
                        + MakeReadableNodeSegment(
                            TEXT("Task"),
                            ChildIndex,
                            Child.ChildTask),
                    KeyDefinitions,
                    Usages);
            }

            if (Child.ChildComposite)
            {
                ScanCompositeRecursive(
                    Tree,
                    Child.ChildComposite,
                    ChildBasePath
                        + TEXT("/")
                        + MakeReadableNodeSegment(
                            TEXT("Composite"),
                            ChildIndex,
                            Child.ChildComposite),
                    KeyDefinitions,
                    Usages);
            }
        }
    }

    static void ScanBehaviorTrees(
        const FString& BlackboardFilter,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FExactUsage>& OutUsages,
        int32& OutBehaviorTreeCount)
    {
        const TArray<FAssetData> BehaviorTreeAssets =
            GetAssetsOfClassInGame(UBehaviorTree::StaticClass());

        OutBehaviorTreeCount = 0;

        for (const FAssetData& AssetData : BehaviorTreeAssets)
        {
            const UBehaviorTree* Tree =
                Cast<UBehaviorTree>(AssetData.GetAsset());

            if (!Tree
                || !Tree->BlackboardAsset
                || !IsBlackboardContextInScope(
                    Tree->BlackboardAsset,
                    BlackboardFilter))
            {
                continue;
            }

            ++OutBehaviorTreeCount;

            for (int32 RootDecoratorIndex = 0;
                 RootDecoratorIndex < Tree->RootDecorators.Num();
                 ++RootDecoratorIndex)
            {
                const UBTDecorator* RootDecorator =
                    Tree->RootDecorators[RootDecoratorIndex];

                ScanNode(
                    Tree,
                    RootDecorator,
                    FString::Printf(
                        TEXT("Root/@RootDecorator[%d](%s)"),
                        RootDecoratorIndex,
                        RootDecorator
                            ? *RootDecorator->GetNodeName()
                            : TEXT("<null>")),
                    KeyDefinitions,
                    OutUsages);
            }

            ScanCompositeRecursive(
                Tree,
                Tree->RootNode,
                FString::Printf(
                    TEXT("Root(%s)"),
                    Tree->RootNode
                        ? *Tree->RootNode->GetNodeName()
                        : TEXT("<null>")),
                KeyDefinitions,
                OutUsages);
        }
    }

    // ---------------------------------------------------------------------
    // Blueprint raw KeyName scan
    //
    // Intentionally does NOT reference UK2Node_CallFunction.
    // UK2Node_CallFunction is MinimalAPI and direct use is linker-fragile
    // across installed UE builds. We identify BlackboardComponent calls by
    // generic graph-node pin signatures instead.
    // ---------------------------------------------------------------------

    static UEdGraphPin* FindInputPin(
        UEdGraphNode* Node,
        const FName PinName)
    {
        if (!Node)
        {
            return nullptr;
        }

        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin
                && Pin->Direction == EGPD_Input
                && Pin->PinName == PinName)
            {
                return Pin;
            }
        }

        return nullptr;
    }

    static FString NormalizeLiteral(FString Value)
    {
        Value.TrimStartAndEndInline();

        if (Value.Len() >= 2
            && Value.StartsWith(TEXT("\""))
            && Value.EndsWith(TEXT("\"")))
        {
            Value = Value.Mid(1, Value.Len() - 2);
        }

        if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            return FString();
        }

        return Value;
    }

    static bool IsNameOutputPin(const UEdGraphPin* Pin)
    {
        if (!Pin)
        {
            return false;
        }

        // K2 Name pins use PinCategory == "name".
        return Pin->PinType.PinCategory == FName(TEXT("name"));
    }

    static bool IsBlackboardComponentCallLikeNode(
        UEdGraphNode* Node,
        UEdGraphPin*& OutKeyNamePin)
    {
        OutKeyNamePin = nullptr;

        if (!Node)
        {
            return false;
        }

        UEdGraphPin* KeyNamePin =
            FindInputPin(Node, FName(TEXT("KeyName")));

        if (!KeyNamePin)
        {
            return false;
        }

        // A UBlackboardComponent member-function call carries an input "self"
        // pin whose object type is UBlackboardComponent (or a subclass).
        UEdGraphPin* SelfPin =
            FindInputPin(Node, FName(TEXT("self")));

        if (!SelfPin)
        {
            return false;
        }

        UObject* SelfTypeObject =
            SelfPin->PinType.PinSubCategoryObject.Get();

        const UClass* SelfClass =
            Cast<UClass>(SelfTypeObject);

        if (!SelfClass
            || !SelfClass->IsChildOf(UBlackboardComponent::StaticClass()))
        {
            return false;
        }

        OutKeyNamePin = KeyNamePin;
        return true;
    }

    static bool TryReadOneHopLiteral(
        UEdGraphPin* KeyPin,
        FString& OutLiteral,
        FString& OutSourceKind)
    {
        OutLiteral.Reset();
        OutSourceKind.Reset();

        if (!KeyPin)
        {
            return false;
        }

        // Direct literal:
        // GetValueAsXxx / SetValueAsXxx
        //     KeyName = "SomeKey"
        if (KeyPin->LinkedTo.Num() == 0)
        {
            const FString Literal =
                NormalizeLiteral(KeyPin->GetDefaultAsString());

            if (!Literal.IsEmpty())
            {
                OutLiteral = Literal;
                OutSourceKind = TEXT("DirectPinLiteral");
                return true;
            }

            return false;
        }

        // MVP: exactly one upstream pin, one hop.
        if (KeyPin->LinkedTo.Num() != 1)
        {
            return false;
        }

        UEdGraphPin* SourcePin = KeyPin->LinkedTo[0];

        if (!SourcePin || !IsNameOutputPin(SourcePin))
        {
            return false;
        }

        UEdGraphNode* SourceNode = SourcePin->GetOwningNode();

        if (!SourceNode)
        {
            return false;
        }

        // Make Literal Name typically exposes Value.
        if (UEdGraphPin* ValuePin =
                FindInputPin(SourceNode, FName(TEXT("Value"))))
        {
            if (ValuePin->LinkedTo.Num() == 0)
            {
                const FString Literal =
                    NormalizeLiteral(ValuePin->GetDefaultAsString());

                if (!Literal.IsEmpty())
                {
                    OutLiteral = Literal;
                    OutSourceKind = TEXT("OneHopValueToName");
                    return true;
                }
            }
        }

        // String -> Name conversion typically exposes InString.
        if (UEdGraphPin* StringPin =
                FindInputPin(SourceNode, FName(TEXT("InString"))))
        {
            if (StringPin->LinkedTo.Num() == 0)
            {
                const FString Literal =
                    NormalizeLiteral(StringPin->GetDefaultAsString());

                if (!Literal.IsEmpty())
                {
                    OutLiteral = Literal;
                    OutSourceKind = TEXT("OneHopStringToName");
                    return true;
                }
            }
        }

        return false;
    }

    static FString GetGraphName(const UEdGraphNode* Node)
    {
        const UEdGraph* Graph = Node ? Node->GetGraph() : nullptr;
        return Graph ? Graph->GetName() : TEXT("<UnknownGraph>");
    }

    static FString GetNodeTitle(const UEdGraphNode* Node)
    {
        if (!Node)
        {
            return TEXT("<UnknownNode>");
        }

        return Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
    }

    static void AddRawLiteralUsage(
        const FString& Literal,
        const FString& SourceKind,
        const UBlueprint* Blueprint,
        const UEdGraphNode* Node,
        const TMultiMap<FName, FString>& KeyNameToIds,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FRawUsage>& OutRawUsages)
    {
        const FName KeyName(*Literal);

        TArray<FString> MatchingKeyIds;
        KeyNameToIds.MultiFind(KeyName, MatchingKeyIds);

        if (MatchingKeyIds.Num() == 0)
        {
            return;
        }

        const FString MatchStatus =
            MatchingKeyIds.Num() == 1
                ? TEXT("RAW_LITERAL_UNIQUE_NAME")
                : TEXT("RAW_LITERAL_AMBIGUOUS_NAME");

        for (const FString& KeyId : MatchingKeyIds)
        {
            FKeyDefinition* Definition = KeyDefinitions.Find(KeyId);

            if (!Definition)
            {
                continue;
            }

            ++Definition->RawLiteralReferenceCount;

            FRawUsage Usage;
            Usage.MatchStatus = MatchStatus;
            Usage.BlackboardPath = Definition->BlackboardPath;
            Usage.KeyName = Definition->KeyName;
            Usage.MatchingBlackboardCount = MatchingKeyIds.Num();
            Usage.BlueprintPath = Blueprint->GetPathName();
            Usage.GraphName = GetGraphName(Node);
            Usage.NodeTitle = GetNodeTitle(Node);
            Usage.NodeClass = Node->GetClass()->GetPathName();
            Usage.NodeGuid =
                Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
            Usage.SourceKind = SourceKind;
            Usage.LiteralValue = Literal;

            OutRawUsages.Add(MoveTemp(Usage));
        }
    }

    static void ScanBlueprintRawAccess(
        const TMultiMap<FName, FString>& KeyNameToIds,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FRawUsage>& OutRawUsages,
        TArray<FDynamicAccess>& OutDynamicAccesses,
        int32& OutBlueprintCount,
        int32& OutBlackboardCallCount)
    {
        const TArray<FAssetData> BlueprintAssets =
            GetAssetsOfClassInGame(UBlueprint::StaticClass());

        OutBlueprintCount = 0;
        OutBlackboardCallCount = 0;

        for (const FAssetData& AssetData : BlueprintAssets)
        {
            UBlueprint* Blueprint =
                Cast<UBlueprint>(AssetData.GetAsset());

            if (!Blueprint)
            {
                continue;
            }

            ++OutBlueprintCount;

            // Engine/UnrealEd only; avoids BlueprintGraph module symbols.
            TArray<UEdGraphNode*> AllNodes;
            FBlueprintEditorUtils::GetAllNodesOfClass<UEdGraphNode>(
                Blueprint,
                AllNodes);

            for (UEdGraphNode* Node : AllNodes)
            {
                if (!Node)
                {
                    continue;
                }

                UEdGraphPin* KeyNamePin = nullptr;

                if (!IsBlackboardComponentCallLikeNode(
                        Node,
                        KeyNamePin))
                {
                    continue;
                }

                ++OutBlackboardCallCount;

                FString Literal;
                FString SourceKind;

                if (TryReadOneHopLiteral(
                        KeyNamePin,
                        Literal,
                        SourceKind))
                {
                    AddRawLiteralUsage(
                        Literal,
                        SourceKind,
                        Blueprint,
                        Node,
                        KeyNameToIds,
                        KeyDefinitions,
                        OutRawUsages);
                }
                else if (KeyNamePin->LinkedTo.Num() > 0)
                {
                    FDynamicAccess Dynamic;
                    Dynamic.BlueprintPath = Blueprint->GetPathName();
                    Dynamic.GraphName = GetGraphName(Node);
                    Dynamic.NodeTitle = GetNodeTitle(Node);
                    Dynamic.NodeClass = Node->GetClass()->GetPathName();
                    Dynamic.NodeGuid =
                        Node->NodeGuid.ToString(
                            EGuidFormats::DigitsWithHyphens);
                    Dynamic.Reason =
                        TEXT(
                            "BlackboardComponent KeyName is connected "
                            "but not a supported one-hop literal");

                    OutDynamicAccesses.Add(MoveTemp(Dynamic));
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // CSV export
    // ---------------------------------------------------------------------

    static bool SaveSummaryCsv(
        const FString& OutputDirectory,
        TArray<FKeyDefinition> Definitions)
    {
        Definitions.Sort(
            [](const FKeyDefinition& A, const FKeyDefinition& B)
            {
                if (A.BlackboardPath != B.BlackboardPath)
                {
                    return A.BlackboardPath < B.BlackboardPath;
                }

                return A.KeyName.ToString() < B.KeyName.ToString();
            });

        FString Csv;
        Csv += TEXT(
            "Blackboard,Key,KeyType,InstanceSynced,"
            "ExactReferenceCount,RawLiteralReferenceCount,Status\n");

        for (const FKeyDefinition& Definition : Definitions)
        {
            FString Status;

            if (Definition.ExactReferenceCount > 0)
            {
                Status = TEXT("HAS_EXACT_REF");
            }
            else if (Definition.RawLiteralReferenceCount > 0)
            {
                Status = TEXT("RAW_ONLY");
            }
            else
            {
                Status = TEXT("NO_STATIC_REF");
            }

            Csv += FString::Printf(
                TEXT("%s,%s,%s,%s,%d,%d,%s\n"),
                *CsvEscape(Definition.BlackboardPath),
                *CsvEscape(Definition.KeyName.ToString()),
                *CsvEscape(Definition.KeyType),
                Definition.bInstanceSynced
                    ? TEXT("\"true\"")
                    : TEXT("\"false\""),
                Definition.ExactReferenceCount,
                Definition.RawLiteralReferenceCount,
                *CsvEscape(Status));
        }

        return FFileHelper::SaveStringToFile(
            Csv,
            *FPaths::Combine(
                OutputDirectory,
                TEXT("BlackboardKeySummary.csv")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    static bool SaveExactReferencesCsv(
        const FString& OutputDirectory,
        TArray<FExactUsage> Usages)
    {
        Usages.Sort(
            [](const FExactUsage& A, const FExactUsage& B)
            {
                if (A.DefiningBlackboardPath != B.DefiningBlackboardPath)
                {
                    return A.DefiningBlackboardPath
                        < B.DefiningBlackboardPath;
                }

                if (A.KeyName != B.KeyName)
                {
                    return A.KeyName.ToString()
                        < B.KeyName.ToString();
                }

                if (A.BehaviorTreePath != B.BehaviorTreePath)
                {
                    return A.BehaviorTreePath
                        < B.BehaviorTreePath;
                }

                return A.TreePath < B.TreePath;
            });

        FString Csv;
        Csv += TEXT(
            "ResolutionStatus,DefiningBlackboard,ContextBlackboard,Key,"
            "BehaviorTree,TreePath,NodeKind,NodeName,NodeClass,"
            "ExecutionIndex,PropertyPath\n");

        for (const FExactUsage& Usage : Usages)
        {
            Csv += FString::Printf(
                TEXT("%s,%s,%s,%s,%s,%s,%s,%s,%s,%u,%s\n"),
                *CsvEscape(Usage.ResolutionStatus),
                *CsvEscape(Usage.DefiningBlackboardPath),
                *CsvEscape(Usage.ContextBlackboardPath),
                *CsvEscape(Usage.KeyName.ToString()),
                *CsvEscape(Usage.BehaviorTreePath),
                *CsvEscape(Usage.TreePath),
                *CsvEscape(Usage.NodeKind),
                *CsvEscape(Usage.NodeName),
                *CsvEscape(Usage.NodeClass),
                Usage.ExecutionIndex,
                *CsvEscape(Usage.PropertyPath));
        }

        return FFileHelper::SaveStringToFile(
            Csv,
            *FPaths::Combine(
                OutputDirectory,
                TEXT("BlackboardKeyReferences.csv")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    static bool SaveRawReferencesCsv(
        const FString& OutputDirectory,
        TArray<FRawUsage> Usages)
    {
        Usages.Sort(
            [](const FRawUsage& A, const FRawUsage& B)
            {
                if (A.BlackboardPath != B.BlackboardPath)
                {
                    return A.BlackboardPath < B.BlackboardPath;
                }

                if (A.KeyName != B.KeyName)
                {
                    return A.KeyName.ToString() < B.KeyName.ToString();
                }

                if (A.BlueprintPath != B.BlueprintPath)
                {
                    return A.BlueprintPath < B.BlueprintPath;
                }

                return A.GraphName < B.GraphName;
            });

        FString Csv;
        Csv += TEXT(
            "MatchStatus,Blackboard,Key,MatchingBlackboardCount,"
            "Blueprint,Graph,NodeTitle,NodeClass,NodeGuid,"
            "SourceKind,LiteralValue\n");

        for (const FRawUsage& Usage : Usages)
        {
            Csv += FString::Printf(
                TEXT("%s,%s,%s,%d,%s,%s,%s,%s,%s,%s,%s\n"),
                *CsvEscape(Usage.MatchStatus),
                *CsvEscape(Usage.BlackboardPath),
                *CsvEscape(Usage.KeyName.ToString()),
                Usage.MatchingBlackboardCount,
                *CsvEscape(Usage.BlueprintPath),
                *CsvEscape(Usage.GraphName),
                *CsvEscape(Usage.NodeTitle),
                *CsvEscape(Usage.NodeClass),
                *CsvEscape(Usage.NodeGuid),
                *CsvEscape(Usage.SourceKind),
                *CsvEscape(Usage.LiteralValue));
        }

        return FFileHelper::SaveStringToFile(
            Csv,
            *FPaths::Combine(
                OutputDirectory,
                TEXT("BlackboardRawReferences.csv")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    static bool SaveDynamicAccessCsv(
        const FString& OutputDirectory,
        TArray<FDynamicAccess> Accesses)
    {
        Accesses.Sort(
            [](const FDynamicAccess& A, const FDynamicAccess& B)
            {
                if (A.BlueprintPath != B.BlueprintPath)
                {
                    return A.BlueprintPath < B.BlueprintPath;
                }

                if (A.GraphName != B.GraphName)
                {
                    return A.GraphName < B.GraphName;
                }

                return A.NodeTitle < B.NodeTitle;
            });

        FString Csv;
        Csv += TEXT(
            "Blueprint,Graph,NodeTitle,NodeClass,NodeGuid,Reason\n");

        for (const FDynamicAccess& Access : Accesses)
        {
            Csv += FString::Printf(
                TEXT("%s,%s,%s,%s,%s,%s\n"),
                *CsvEscape(Access.BlueprintPath),
                *CsvEscape(Access.GraphName),
                *CsvEscape(Access.NodeTitle),
                *CsvEscape(Access.NodeClass),
                *CsvEscape(Access.NodeGuid),
                *CsvEscape(Access.Reason));
        }

        return FFileHelper::SaveStringToFile(
            Csv,
            *FPaths::Combine(
                OutputDirectory,
                TEXT("BlackboardDynamicKeyAccess.csv")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
}

void FBlackboardUsageScanner::Run(const TArray<FString>& Args)
{
    using namespace BlackboardUsageScanner;

    const FString BlackboardFilter =
        Args.Num() > 0
            ? Args[0]
            : FString();

    UE_LOG(
        LogBlackboardUsageScanner,
        Display,
        TEXT("Starting Blackboard scan under /Game. Filter='%s'"),
        *BlackboardFilter);

    TMap<FString, FKeyDefinition> KeyDefinitions;
    TMultiMap<FName, FString> KeyNameToIds;

    TArray<FExactUsage> ExactUsages;
    TArray<FRawUsage> RawUsages;
    TArray<FDynamicAccess> DynamicAccesses;

    int32 BlackboardCount = 0;
    int32 BehaviorTreeCount = 0;
    int32 BlueprintCount = 0;
    int32 BlackboardCallCount = 0;

    BuildBlackboardRegistry(
        BlackboardFilter,
        KeyDefinitions,
        KeyNameToIds,
        BlackboardCount);

    if (KeyDefinitions.Num() == 0)
    {
        UE_LOG(
            LogBlackboardUsageScanner,
            Error,
            TEXT(
                "No Blackboard keys matched filter '%s'. "
                "Try: BB.ScanUsage BB_QuakeBoard"),
            *BlackboardFilter);
        return;
    }

    ScanBehaviorTrees(
        BlackboardFilter,
        KeyDefinitions,
        ExactUsages,
        BehaviorTreeCount);

    ScanBlueprintRawAccess(
        KeyNameToIds,
        KeyDefinitions,
        RawUsages,
        DynamicAccesses,
        BlueprintCount,
        BlackboardCallCount);

    int32 UnresolvedExactCount = 0;

    for (const FExactUsage& Usage : ExactUsages)
    {
        if (Usage.ResolutionStatus == TEXT("UNRESOLVED_KEY"))
        {
            ++UnresolvedExactCount;
        }
    }

    const int32 ExactUsageCount = ExactUsages.Num();
    const int32 RawUsageCount = RawUsages.Num();
    const int32 DynamicAccessCount = DynamicAccesses.Num();

    TArray<FKeyDefinition> DefinitionArray;
    KeyDefinitions.GenerateValueArray(DefinitionArray);

    const FString SafeFilter =
        BlackboardFilter.IsEmpty()
            ? TEXT("All")
            : FPaths::MakeValidFileName(
                BlackboardFilter,
                TEXT('_'));

    const FString OutputDirectory =
        FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("BlackboardUsageScanner"),
            SafeFilter);

    IFileManager::Get().MakeDirectory(
        *OutputDirectory,
        true);

    const bool bSummarySaved =
        SaveSummaryCsv(
            OutputDirectory,
            MoveTemp(DefinitionArray));

    const bool bExactSaved =
        SaveExactReferencesCsv(
            OutputDirectory,
            MoveTemp(ExactUsages));

    const bool bRawSaved =
        SaveRawReferencesCsv(
            OutputDirectory,
            MoveTemp(RawUsages));

    const bool bDynamicSaved =
        SaveDynamicAccessCsv(
            OutputDirectory,
            MoveTemp(DynamicAccesses));

    if (bSummarySaved && bExactSaved && bRawSaved && bDynamicSaved)
    {
        UE_LOG(
            LogBlackboardUsageScanner,
            Display,
            TEXT(
                "Scan complete. BB=%d, BT=%d, Blueprints=%d, "
                "BlackboardCalls=%d, ExactRefs=%d, RawLiteralRefs=%d, "
                "DynamicKeyCalls=%d, UnresolvedExact=%d. Output: %s"),
            BlackboardCount,
            BehaviorTreeCount,
            BlueprintCount,
            BlackboardCallCount,
            ExactUsageCount,
            RawUsageCount,
            DynamicAccessCount,
            UnresolvedExactCount,
            *FPaths::ConvertRelativePathToFull(OutputDirectory));

        UE_LOG(
            LogBlackboardUsageScanner,
            Warning,
            TEXT(
                "NO_STATIC_REF is not proof of unused. "
                "Review BlackboardDynamicKeyAccess.csv before deletion."));
    }
    else
    {
        UE_LOG(
            LogBlackboardUsageScanner,
            Error,
            TEXT("Failed to save one or more Blackboard usage CSV files."));
    }
}
