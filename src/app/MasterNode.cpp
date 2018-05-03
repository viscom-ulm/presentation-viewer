/**
* @file   MasterNode.cpp
* @author Sebastian Maisch <sebastian.maisch@uni-ulm.de>
* @date   2016.11.25
*
* @brief  Implementation of the master application node.
*/

#include "MasterNode.h"
#include "Filesystem.h"
#include <imgui.h>
#include <iostream>
#include <experimental/filesystem>


namespace viscom {

    MasterNode::MasterNode(ApplicationNodeInternal* appNode) :
        ApplicationNodeImplementation{appNode},
        current_slide_(0),
        numberOfSlides_(0),
        inputDir_("D:/dev"),
        inputDirectorySelected_(false),
        allTexturesInitialized_(false)
    {
    }

    void MasterNode::InitOpenGL()
    {
        ApplicationNodeImplementation::InitOpenGL();
    }

    MasterNode::~MasterNode() = default;

    void MasterNode::Draw2D(FrameBuffer& fbo)
    {
        std::vector<std::string> supportedDriveLetters = { "A:/", "B:/", "C:/", "D:/", "E:/", "F:/", "G:/", "H:/" };
        namespace fs = std::experimental::filesystem;
        fbo.DrawToFBO([&]() {            
            ImGui::SetNextWindowPos(ImVec2(700, 60), ImGuiSetCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(550, 680), ImGuiCond_FirstUseEver);
            if (!inputDirectorySelected_ && ImGui::Begin("Select input directory", nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_ShowBorders))
            {
                ImGui::Text(inputDir_.data());
                for (const auto& dl : supportedDriveLetters) {
                    if (fs::is_directory(dl)) {
                        bool selected = false;
                        ImGui::Selectable(dl.c_str(), &selected);
                        if (selected) inputDir_ = dl;
                    }
                }
                for(auto &path : getDirectoryContent(inputDir_))
                {
                    bool selected = false;
                    ImGui::Selectable(path.data(), &selected);
                    if(selected) inputDir_ = fs::canonical(fs::path(inputDir_) / path).string();
                }

                inputDirectorySelected_ = ImGui::Button("Select Folder", ImVec2(50, 20));
                if(inputDirectorySelected_)
                {
                    loadSlides();
                }
                ImGui::End();
            }
            
        });
        ApplicationNodeImplementation::Draw2D(fbo);
    }

    void MasterNode::loadSlides() {
        texture_slides_.clear();
        auto slideNumber = 1;
        std::vector<std::string> slides = getFiles(inputDir_);
        std::sort(slides.begin(), slides.end(), viscom::comparePaths);

        for(auto& slide : slides)
        {
            const auto texture = GetTextureManager().GetResource(slide);
            if (!texture) break;
            texture_slides_.push_back(texture);
        }
        numberOfSlides_ = texture_slides_.size();

        allTexturesInitialized_ = false;
        presentationInitialized_.resize(0);
        clientReceivedTexture_.resize(0);

#ifdef VISCOM_USE_SGCT
        presentationInitialized_.resize(sgct_core::ClusterManager::instance()->getNumberOfNodes(), false);
        presentationInitialized_[sgct_core::ClusterManager::instance()->getThisNodeId()] = true;
        clientReceivedTexture_.resize(sgct_core::ClusterManager::instance()->getNumberOfNodes(), std::vector<bool>(numberOfSlides_, false));
        clientReceivedTexture_[sgct_core::ClusterManager::instance()->getThisNodeId()] = std::vector<bool>(numberOfSlides_, true);
#else
        setCurrentTexture(texture_slides_[current_slide_]->getTextureId());
#endif

        animationChanged_ = true;

    }

    bool MasterNode::KeyboardCallback(int key, int scancode, int action, int mods)
    {
        if (ApplicationNodeBase::KeyboardCallback(key, scancode, action, mods)) return true;

        switch (key)
        {
        case GLFW_KEY_LEFT:
            if (action == GLFW_REPEAT || action == GLFW_PRESS) {
                if (current_slide_ - 1 >= 0) {
                    current_slide_--;
                }
                setCurrentTexture(texture_slides_[current_slide_]->getTextureId());
                return true;
            }
            break;
        case GLFW_KEY_RIGHT:
            if (action == GLFW_REPEAT || action == GLFW_PRESS) {
                if (current_slide_ + 1 < numberOfSlides_) {
                    current_slide_++;
                }
                setCurrentTexture(texture_slides_[current_slide_]->getTextureId());
                return true;
            }
            break;
        case GLFW_KEY_L:
            if (action == GLFW_PRESS && mods == GLFW_MOD_CONTROL) {
                inputDirectorySelected_ = false;
                return true;
            }
            break;
        default: return false;
        }

        return false;
    }

    std::vector<std::string> MasterNode::getDirectoryContent(const std::string& dir, const bool returnFiles) const
    {
        namespace fs = std::experimental::filesystem;

        std::vector<std::string> content;
        if (!returnFiles) content.emplace_back("..");

        // apparently stb image seems to have problems loading jpg files...
        auto checkImageFile = [](const fs::path& p) { return (p.extension().string() == ".png"); }; // || p.extension().string() == ".jpg" || p.extension().string() == ".jpeg"); };

        for (auto& p : fs::directory_iterator(dir)) {
            if (returnFiles && fs::is_regular_file(p) && checkImageFile(p)) content.push_back(fs::absolute(p, dir).string());
            else if (!returnFiles && fs::is_directory(p)) content.push_back(p.path().filename().string());
        }
        return content;
    }
#ifdef VISCOM_USE_SGCT
    bool MasterNode::DataTransferCallback(void* receivedData, int receivedLength, std::uint16_t packageID, int clientID)
    {
        switch (PackageID(packageID))
        {
        case 0:
        {
            const auto state = *reinterpret_cast<ClientState*>(receivedData);
            if (state.numberOfSlides >= 0) {
                LOG(INFO) << "client " << state.clientId << " synced presentation " << state.numberOfSlides << " slides.";
                presentationInitialized_[state.clientId] = true;
            } else {
                LOG(INFO) << "client " << state.clientId << " synced texture " << state.textureIndex;
                clientReceivedTexture_[state.clientId][state.textureIndex] = true;
            }
        } break;
        case 1:
        {
            if (!presentationInitialized_.empty()) {
                const auto state = *reinterpret_cast<ClientState*>(receivedData);
                presentationInitialized_[state.clientId] = false;
                for (auto& texInit : clientReceivedTexture_[state.clientId]) texInit = false;
                allTexturesInitialized_ = false;
            }
        } break;
        default : break;
        }

        return true;
    }

    void MasterNode::TransferSlide(std::size_t slideID, std::size_t clientId) const
    {
        const auto& tex = texture_slides_[slideID];
        SlideTexDescriptor slideTexDesc{ tex->getDescriptor() };
        auto dim = tex->getDimensions();
        slideTexDesc.width_ = dim.x;
        slideTexDesc.height_ = dim.y;

        std::vector<std::uint8_t> textureData;
        GetTextureData(textureData, slideTexDesc, tex->getTextureId());

        TextureHeaderMessage mm(numberOfSlides_, slideID, slideTexDesc);

        std::vector<std::uint8_t> messageData;
        messageData.resize(sizeof(TextureHeaderMessage) + textureData.size());
        memcpy(messageData.data(), &mm, sizeof(TextureHeaderMessage));
        memcpy(messageData.data() + sizeof(TextureHeaderMessage), textureData.data(), textureData.size());
        sgct::Engine::instance()->transferDataToNode(messageData.data(), static_cast<int>(messageData.size()), PackageID::TextureData, clientId);
    }

    void MasterNode::EncodeData()
    {
        ApplicationNodeImplementation::EncodeData();
        sgct::SharedData::instance()->writeInt32(&sharedIndex_);
    }

    void MasterNode::PreSync()
    {
        ApplicationNodeImplementation::PreSync();
        sharedIndex_.setVal(current_slide_);
    }
#endif

    void MasterNode::GetTextureData(std::vector<uint8_t>& data, const SlideTexDescriptor& desc, GLuint textureId) const
    {
        std::size_t size = desc.width_ * desc.height_ * desc.desc_.bytesPP_;
        data.resize(size);
        assert(data.size() != 0);

        GLuint pbo = 0;
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, size, nullptr, GL_STREAM_READ);

        glBindTexture(GL_TEXTURE_2D, textureId);
        glGetTexImage(GL_TEXTURE_2D, 0, desc.desc_.format_, desc.desc_.type_, nullptr);

        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        auto gpuMem = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (gpuMem) {
            memcpy(data.data(), gpuMem, size);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        glDeleteBuffers(1, &pbo);
    }

    void MasterNode::UpdateFrame(double currentTime, double elapsedTime)
    {
        ApplicationNodeImplementation::UpdateFrame(currentTime, elapsedTime);

#ifdef VISCOM_USE_SGCT
        if (!allTexturesInitialized_) {
            bool sentMessage = false;
            // try initialize presentation.
            for (std::size_t i = 0; i < presentationInitialized_.size(); ++i) {
                if (sentMessage || presentationInitialized_[i]) continue;

                // i-1 works only if the first node is the master!!
                sgct::Engine::instance()->transferDataToNode(&numberOfSlides_, sizeof(std::size_t), PackageID::PresentationData, i - 1);
                sentMessage = true;
                return;
            }

            for (std::size_t i = 0; i < clientReceivedTexture_.size(); ++i) {
                for (std::size_t iT = 0; iT < clientReceivedTexture_[i].size(); ++iT) {
                    if (sentMessage || clientReceivedTexture_[i][iT]) continue;

                    // i-1 works only if the first node is the master!!
                    TransferSlide(iT, i - 1);
                    sentMessage = true;
                    return;
                }
            }

            if (!sentMessage) {
                allTexturesInitialized_ = true;
                if (!texture_slides_.empty() && animationChanged_) {
                    current_slide_ = 0;
                    setCurrentTexture(texture_slides_[current_slide_]->getTextureId());
                    animationChanged_ = false;
                }
            }
        }
#endif
    }

}
