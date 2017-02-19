/**
* Copyright (C) 2016 Johannes Ohlemacher (https://github.com/eXistence/TeeTime-Cpp-Examples)
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*         http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <teetime/Configuration.h>
#include <teetime/stages/InitialElementProducer.h>
#include <teetime/stages/Directory2Files.h>
#include <teetime/stages/File2FileBuffer.h>
#include <teetime/stages/ReadImage.h>
#include <teetime/stages/FunctionStage.h>
#include <teetime/stages/CollectorSink.h>
#include <teetime/stages/DistributorStage.h>
#include <teetime/stages/MergerStage.h>
#include <teetime/Image.h>
#include <utility>
#include <algorithm>

struct MipMapSettings
{
  unsigned numThreads = std::thread::hardware_concurrency();
  std::string inputDirectory;
  std::string outputDirectory;
};

class MipMapConfiguration : public teetime::Configuration
{
public:
  using Image = std::pair<std::string, teetime::Image>;

  explicit MipMapConfiguration(const MipMapSettings& settings)
  {
    auto producer = createStage<teetime::InitialElementProducer<std::string>>(settings.inputDirectory);
    auto dir2files = createStage<teetime::Directory2Files>();
    auto distributor = createStage<teetime::DistributorStage<teetime::File>>();
    auto merger = createStage<teetime::MergerStage<std::string>>();
    auto collector = createStage<teetime::CollectorSink<std::string>>();

    declareStageActive(producer);
    declareStageActive(merger);

    connectPorts(producer->getOutputPort(), dir2files->getInputPort());
    connectPorts(dir2files->getOutputPort(), distributor->getInputPort());
    connectPorts(merger->getOutputPort(), collector->getInputPort());

    for (unsigned i = 0; i < settings.numThreads; ++i)
    {
      auto readimage = createStageFromLambda([](teetime::File&& file) {
        Image image;
        auto split = std::find_if(file.path.rbegin(), file.path.rend(), [](char c) { return c == '/' || c == '\\';  });
        image.first = std::string(split.base(), file.path.end());
        image.second.loadFromFile(file.path);
        return image;
      });

      auto resizeimage = createStageFromLambda([](Image&& image) {
        image.second = image.second.resize(image.second.getWidth() / 2, image.second.getHeight() / 2);
        return image;
      });

      auto saveimage = createStageFromLambda([=](Image&& image) {
        auto filename = settings.outputDirectory + "/" + image.first + ".png";
        auto split = std::find_if(filename.rbegin(), filename.rend(), [](char c) { return c == '/' || c == '\\';  });
        auto dir = std::string(filename.begin(), split.base());
        if (!teetime::platform::isDirectory(dir)) {
          teetime::platform::createDirectory(dir);
        }
        image.second.saveToFile(filename);
        return filename;
      });

      declareStageActive(readimage);

      connectPorts(distributor->getNewOutputPort(), readimage->getInputPort());
      connectPorts(readimage->getOutputPort(), resizeimage->getInputPort());
      connectPorts(resizeimage->getOutputPort(), saveimage->getInputPort());
      connectPorts(saveimage->getOutputPort(), merger->getNewInputPort());
    }
  }

private:
};

int main(int argc, char** argv)
{
  teetime::setLogCallback(teetime::simpleLogging);
  teetime::setLogLevel(teetime::LogLevel::Info);

  MipMapSettings settings;
  for (int i = 1; (i+1) < argc; ++i) {
    std::string name = argv[i];
    if (name == "--input")
    {
      settings.inputDirectory = argv[++i];
      continue;
    }

    if (name == "--output")
    {
      settings.outputDirectory = argv[++i];
      continue;
    }

    if (name == "--threads")
    {
      int n = ::atoi(argv[++i]);
      if (n <= 0) {
        TEETIME_ERROR() << "number of threads must be >0";
        return EXIT_FAILURE;
      }
      settings.numThreads = static_cast<unsigned>(n);
      continue;
    }
  }

  if (settings.inputDirectory.empty()) 
  {
    TEETIME_ERROR() << "no input directory";
    return EXIT_FAILURE;
  }

  if (!teetime::platform::isDirectory(settings.inputDirectory))
  {
    TEETIME_ERROR() << "input directory does not exist: " << settings.inputDirectory;
    return EXIT_FAILURE;
  }

  if (settings.outputDirectory.empty()) 
  {
    TEETIME_ERROR() << "no output directory";
    return EXIT_FAILURE;
  }

  if (!teetime::platform::isDirectory(settings.outputDirectory) && 
    !teetime::platform::createDirectory(settings.outputDirectory))
  {
    TEETIME_ERROR() << "failed to create output directory: " << settings.inputDirectory;
    return EXIT_FAILURE;
  }

  MipMapConfiguration configuration(settings);

  auto start = teetime::platform::microSeconds();
  configuration.executeBlocking();
  auto end = teetime::platform::microSeconds();
  TEETIME_INFO() << "threads: " << settings.numThreads << ", time: " << (end - start) << "us";

  return EXIT_SUCCESS;
}

