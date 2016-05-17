////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Project:  Embedded Machine Learning Library (EMLL)
//  File:     main.cpp (stochasticGradientDescentTrainer)
//  Authors:  Ofer Dekel
//
////////////////////////////////////////////////////////////////////////////////////////////////////

// utilities
#include "Files.h"
#include "OutputStreamImpostor.h" 
#include "CommandLineParser.h" 
#include "RandomEngines.h"
#include "BinaryClassificationEvaluator.h"

// layers
#include "Map.h"
#include "Coordinate.h"
#include "CoordinateListTools.h"

// dataset
#include "SupervisedExample.h"

// common
#include "sgdIncrementalTrainerArguments.h"
#include "MultiEpochIncrementalTrainerArguments.h"
#include "TrainerArguments.h"
#include "MapLoadArguments.h" 
#include "MapSaveArguments.h" 
#include "DataLoadArguments.h" 
#include "DataLoaders.h"
#include "LoadModel.h"
#include "MakeTrainer.h"
#include "MakeEvaluator.h"

// trainers
#include "SGDIncrementalTrainer.h"
#include "MultiEpochIncrementalTrainer.h"
#include "SingleEpochTrainer.h"

// lossFunctions
#include "HingeLoss.h"
#include "LogLoss.h"

// stl
#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[])
{
    try
    {
        // create a command line parser
        utilities::CommandLineParser commandLineParser(argc, argv);

        // add arguments to the command line parser
        common::ParsedTrainerArguments trainerArguments;
        common::ParsedMapLoadArguments mapLoadArguments;
        common::ParsedDataLoadArguments dataLoadArguments;
        common::ParsedMapSaveArguments mapSaveArguments;
        common::ParsedSGDIncrementalTrainerArguments sgdIncrementalTrainerArguments;
        common::ParsedMultiEpochIncrementalTrainerArguments multiEpochTrainerArguments;

        commandLineParser.AddOptionSet(trainerArguments);
        commandLineParser.AddOptionSet(mapLoadArguments);
        commandLineParser.AddOptionSet(dataLoadArguments);
        commandLineParser.AddOptionSet(mapSaveArguments);
        commandLineParser.AddOptionSet(multiEpochTrainerArguments);
        commandLineParser.AddOptionSet(sgdIncrementalTrainerArguments);
        
        // parse command line
        commandLineParser.Parse();

        if(trainerArguments.verbose) std::cout << "Stochastic Gradient Descent Trainer" << std::endl;

        // if output file specified, replace stdout with it 
        auto outStream = utilities::GetOutputStreamImpostor(mapSaveArguments.outputModelFile);

        // load a model
        auto model = common::LoadModel(mapLoadArguments.modelLoadArguments);

        // get output coordinate list and create the map
        auto outputCoordinateList = layers::BuildCoordinateList(model, dataLoadArguments.parsedDataDimension, mapLoadArguments.coordinateListString);
        layers::Map map(model, outputCoordinateList);

        // load dataset
        if(trainerArguments.verbose) std::cout << "Loading data ..." << std::endl;
        auto rowDataset = common::GetRowDataset(dataLoadArguments, map);

        // create sgd trainer
        auto trainer = common::MakeMultiEpochSGDIncrementalTrainer(outputCoordinateList.Size(), trainerArguments.lossArguments, sgdIncrementalTrainerArguments, multiEpochTrainerArguments);

        // create evaluator - TODO
        auto evaluator = common::MakeBinaryClassificationEvaluator<predictors::LinearPredictor>(trainerArguments.lossArguments);

        // train
        if(trainerArguments.verbose) std::cout << "Training ..." << std::endl;
        auto trainSetIterator = rowDataset.GetIterator();
        trainer->Update(trainSetIterator);
        const auto& predictor = trainer->GetPredictor();

        // print loss and errors
        if(trainerArguments.verbose)
        {
            std::cout << "Finished training.\n";
            std::cout << "Training error\n";
            evaluator->Print(std::cout); // TODO
            std::cout << std::endl;
        }

        // update the map with the newly learned layers
       // auto predictor = trainer->GetPredictor();

        predictor.AddToModel(model, outputCoordinateList);

        // output map
        model.Save(outStream);
    }
    catch (const utilities::CommandLineParserPrintHelpException& exception)
    {
        std::cout << exception.GetHelpText() << std::endl;
        return 0;
    }
    catch (const utilities::CommandLineParserErrorException& exception)
    {
        std::cerr << "Command line parse error:" << std::endl;
        for (const auto& error : exception.GetParseErrors())
        {
            std::cerr << error.GetMessage() << std::endl;
        }
        return 1;
    }
    catch (std::runtime_error exception)
    {
        std::cerr << "runtime error: " << exception.what() << std::endl;
        return 1;
    }

    return 0;
}
