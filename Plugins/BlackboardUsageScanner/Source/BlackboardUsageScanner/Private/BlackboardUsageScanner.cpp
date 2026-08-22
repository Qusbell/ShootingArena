#include "BlackboardUsageScanner.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
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
        FName KeyName = NAME_None;
        FString KeyType;
        bool bInstanceSynced = false;
        int32 ExactReferenceCount = 0;
    };

    struct FUsage
    {
        FString ResolutionStatus; // RESOLVED / UNRESOLVED_KEY

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

    static FString MakeKeyId(const FString& BlackboardPath, const FName KeyName)
    {
        return BlackboardPath + TEXT("::") + KeyName.ToString();
    }

    static FString CsvEscape(FString Value)
    {
        Value.ReplaceInline(TEXT("\""), TEXT("\"\""));
        return FString::Printf(TEXT("\"%s\""), *Value);
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
                    // Child local keys win over inherited parent keys.
                    return Blackboard;
                }
            }
        }

        return nullptr;
    }

    static void AddUsage(
        const UBehaviorTree* Tree,
        const UBTNode* Node,
        const FString& TreePath,
        const FString& PropertyPath,
        const FBlackboardKeySelector& Selector,
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FUsage>& Usages)
    {
        if (!Tree || !Node || Selector.SelectedKeyName.IsNone())
        {
            return;
        }

        FUsage Usage;
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
            FindDefiningBlackboard(Tree->BlackboardAsset, Selector.SelectedKeyName);

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
                FKeyDefinition NewDefinition;
                NewDefinition.BlackboardPath = Usage.DefiningBlackboardPath;
                NewDefinition.KeyName = Usage.KeyName;
                NewDefinition.KeyType = TEXT("<unknown/outside scan scope>");
                NewDefinition.ExactReferenceCount = 1;

                KeyDefinitions.Add(KeyId, MoveTemp(NewDefinition));
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
        TArray<FUsage>& Usages)
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
                    AddUsage(
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

            // Support selectors nested inside another USTRUCT.
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
        TArray<FUsage>& Usages)
    {
        if (!Tree || !Node)
        {
            return;
        }

        // Includes inherited UPROPERTY fields from native/BP-generated classes.
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
        TArray<FUsage>& Usages)
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
        TArray<FUsage>& Usages)
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

    static void BuildBlackboardRegistry(
        TMap<FString, FKeyDefinition>& OutKeyDefinitions,
        int32& OutBlackboardCount)
    {
        const TArray<FAssetData> BlackboardAssets =
            GetAssetsOfClassInGame(UBlackboardData::StaticClass());

        OutBlackboardCount = BlackboardAssets.Num();

        for (const FAssetData& AssetData : BlackboardAssets)
        {
            const UBlackboardData* Blackboard =
                Cast<UBlackboardData>(AssetData.GetAsset());

            if (!Blackboard)
            {
                continue;
            }

            for (const FBlackboardEntry& Entry : Blackboard->Keys)
            {
                FKeyDefinition Definition;
                Definition.BlackboardPath = Blackboard->GetPathName();
                Definition.KeyName = Entry.EntryName;
                Definition.KeyType =
                    Entry.KeyType
                        ? Entry.KeyType->GetClass()->GetName()
                        : TEXT("<None>");
                Definition.bInstanceSynced = Entry.bInstanceSynced != 0;

                OutKeyDefinitions.Add(
                    MakeKeyId(Definition.BlackboardPath, Definition.KeyName),
                    MoveTemp(Definition));
            }
        }
    }

    static void ScanBehaviorTrees(
        TMap<FString, FKeyDefinition>& KeyDefinitions,
        TArray<FUsage>& OutUsages,
        int32& OutBehaviorTreeCount)
    {
        const TArray<FAssetData> BehaviorTreeAssets =
            GetAssetsOfClassInGame(UBehaviorTree::StaticClass());

        OutBehaviorTreeCount = BehaviorTreeAssets.Num();

        for (const FAssetData& AssetData : BehaviorTreeAssets)
        {
            const UBehaviorTree* Tree =
                Cast<UBehaviorTree>(AssetData.GetAsset());

            if (!Tree)
            {
                continue;
            }

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
            "Blackboard,Key,KeyType,InstanceSynced,ExactReferenceCount,Status\n");

        for (const FKeyDefinition& Definition : Definitions)
        {
            const FString Status =
                Definition.ExactReferenceCount > 0
                    ? TEXT("HAS_EXACT_REF")
                    : TEXT("NO_EXACT_REF");

            Csv += FString::Printf(
                TEXT("%s,%s,%s,%s,%d,%s\n"),
                *CsvEscape(Definition.BlackboardPath),
                *CsvEscape(Definition.KeyName.ToString()),
                *CsvEscape(Definition.KeyType),
                Definition.bInstanceSynced
                    ? TEXT("\"true\"")
                    : TEXT("\"false\""),
                Definition.ExactReferenceCount,
                *CsvEscape(Status));
        }

        return FFileHelper::SaveStringToFile(
            Csv,
            *FPaths::Combine(
                OutputDirectory,
                TEXT("BlackboardKeySummary.csv")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    static bool SaveReferencesCsv(
        const FString& OutputDirectory,
        TArray<FUsage> Usages)
    {
        Usages.Sort(
            [](const FUsage& A, const FUsage& B)
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

        for (const FUsage& Usage : Usages)
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
}

void FBlackboardUsageScanner::Run()
{
    using namespace BlackboardUsageScanner;

    UE_LOG(
        LogBlackboardUsageScanner,
        Display,
        TEXT("Starting exact Blackboard key usage scan under /Game ..."));

    TMap<FString, FKeyDefinition> KeyDefinitions;
    TArray<FUsage> Usages;

    int32 BlackboardCount = 0;
    int32 BehaviorTreeCount = 0;

    BuildBlackboardRegistry(
        KeyDefinitions,
        BlackboardCount);

    ScanBehaviorTrees(
        KeyDefinitions,
        Usages,
        BehaviorTreeCount);

    TArray<FKeyDefinition> DefinitionArray;
    KeyDefinitions.GenerateValueArray(DefinitionArray);

    const FString OutputDirectory =
        FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("BlackboardUsageScanner"));

    IFileManager::Get().MakeDirectory(
        *OutputDirectory,
        true);

    int32 UnresolvedCount = 0;
    for (const FUsage& Usage : Usages)
    {
        if (Usage.ResolutionStatus == TEXT("UNRESOLVED_KEY"))
        {
            ++UnresolvedCount;
        }
    }

    const int32 ExactUsageCount = Usages.Num();

    const bool bSummarySaved =
        SaveSummaryCsv(
            OutputDirectory,
            MoveTemp(DefinitionArray));

    const bool bReferencesSaved =
        SaveReferencesCsv(
            OutputDirectory,
            MoveTemp(Usages));

    if (bSummarySaved && bReferencesSaved)
    {
        UE_LOG(
            LogBlackboardUsageScanner,
            Display,
            TEXT(
                "Blackboard scan complete. BB=%d, BT=%d, "
                "ExactRefs=%d, Unresolved=%d. Output: %s"),
            BlackboardCount,
            BehaviorTreeCount,
            ExactUsageCount,
            UnresolvedCount,
            *FPaths::ConvertRelativePathToFull(OutputDirectory));

        UE_LOG(
            LogBlackboardUsageScanner,
            Warning,
            TEXT(
                "MVP limitation: NO_EXACT_REF does NOT mean unused. "
                "Raw FName/FString BlackboardComponent accesses and "
                "Blueprint graph literals are not scanned yet."));
    }
    else
    {
        UE_LOG(
            LogBlackboardUsageScanner,
            Error,
            TEXT("Failed to save one or more Blackboard usage CSV files."));
    }
}
