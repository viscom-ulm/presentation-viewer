/**
 * @file   CoordinatorNode.h
 * @author Sebastian Maisch <sebastian.maisch@uni-ulm.de>
 * @date   2016.11.25
 *
 * @brief  Declaration of the ApplicationNodeImplementation for the coordinator node.
 */

#pragma once

#include "core/open_gl.h"
#include "app/ApplicationNodeImplementation.h"

namespace viscom {

    enum PackageID : int {
        PresentationData = 0,
        TextureData = 1
    };
    struct ClientState
    {
        ClientState() {}
        ClientState(int cId) : clientId(cId), textureIndex(-1), numberOfSlides(-1) {}
        int clientId = -1;
        int textureIndex = -1;
        int numberOfSlides = -1;
    };

    struct SlideTexDescriptor
    {
        SlideTexDescriptor() noexcept : desc_{ 0, 0, 0, 0 } {}
        SlideTexDescriptor(const TextureDescriptor& desc) noexcept : desc_{ desc } {}

        TextureDescriptor desc_;
        std::size_t width_ = 0;
        std::size_t height_ = 0;
    };

    struct TextureHeaderMessage
    {
        TextureHeaderMessage() noexcept : index(-1), descriptor(TextureDescriptor{ 0, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE }) {}
        TextureHeaderMessage(std::size_t nos, std::size_t i, SlideTexDescriptor des) : index(i), descriptor(des) { }
        std::size_t index;
        SlideTexDescriptor descriptor;
    };

    class CoordinatorNode final : public ApplicationNodeImplementation
    {
    public:
        explicit CoordinatorNode(ApplicationNodeInternal* appNode);
        virtual ~CoordinatorNode() override;

        void Draw2D(FrameBuffer& fbo) override;

        virtual bool KeyboardCallback(int key, int scancode, int action, int mods) override;
        std::vector<std::string> getDirectoryContent(const std::string &dir, bool returnFiles = false) const;
        std::vector<std::string> getFiles(const std::string &dir) { return getDirectoryContent(dir, true); }


        /** iterates over resource/slides folder and loads textures */
        void loadSlides();

#ifdef VISCOM_USE_SGCT
        virtual void EncodeData() override;
        virtual void PreSync() override;
        virtual bool DataTransferCallback(void* receivedData, int receivedLength, std::uint16_t packageID, int clientID) override;
#endif
    private:
#ifdef VISCOM_USE_SGCT
        sgct::SharedInt32 sharedIndex_;
#endif
        /* Holds the input directory containing slides*/
        std::string inputDir_;

        bool inputDirectorySelected_;
    };
}
