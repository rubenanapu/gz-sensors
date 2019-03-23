/*
 * Copyright (C) 2017 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <ignition/math/Helpers.hh>

#include "ignition/sensors/CameraSensor.hh"
#include "ignition/sensors/SensorFactory.hh"

using namespace ignition;
using namespace sensors;

/// \brief Private data for CameraSensor
class ignition::sensors::CameraSensorPrivate
{
  /// \brief Remove a camera from a scene
  public: void RemoveCamera(ignition::rendering::ScenePtr _scene);

  /// \brief Save an image
  /// \param[in] _data the image data to be saved
  /// \param[in] _width width of image in pixels
  /// \param[in] _height height of image in pixels
  /// \param[in] _format The format the data is in
  /// \return True if the image was saved successfully. False can mean
  /// that the path provided to the constructor does exist and creation
  /// of the path was not possible.
  /// \sa ImageSaver
  public: bool SaveImage(const unsigned char *_data, unsigned int _width,
    unsigned int _height, ignition::common::Image::PixelFormatType _format);

  /// \brief node to create publisher
  public: transport::Node node;

  /// \brief publisher to publish images
  public: transport::Node::Publisher pub;

  /// \brief true if Load() has been called and was successful
  public: bool initialized = false;

  /// \brief Rendering camera
  public: ignition::rendering::CameraPtr camera;

  /// \brief Pointer to an image to be published
  public: ignition::rendering::Image image;

  /// \brief Event that is used to trigger callbacks when a new image
  /// is generated
  public: ignition::common::EventT<
          void(const ignition::msgs::Image &)> imageEvent;

  /// \brief Connection to the Manager's scene change event.
  public: ignition::common::ConnectionPtr sceneChangeConnection;

  /// \brief Just a mutex for thread safety
  public: std::mutex mutex;

  /// \brief True to save images
  public: bool saveImage = false;

  /// \brief path directory to where images are saved
  public: std::string saveImagePath = "";

  /// \prefix of an image name
  public: std::string saveImagePrefix = "";

  /// \brief counter used to set the image filename
  public: std::uint64_t saveImageCounter = 0;
};

//////////////////////////////////////////////////
bool CameraSensor::CreateCamera()
{
  sdf::ElementPtr cameraElem = this->SDF()->GetElement("camera");
  if (!cameraElem)
  {
    ignerr << "Unable to find <camera> SDF element\n";
    return false;
  }

  sdf::ElementPtr imgElem = cameraElem->GetElement("image");

  if (!imgElem)
  {
    ignerr << "Unable to find <camera><image> SDF element\n";
    return false;
  }

  int width = imgElem->Get<int>("width");
  int height = imgElem->Get<int>("height");

  this->dataPtr->camera = this->Scene()->CreateCamera(this->Name());
  this->dataPtr->camera->SetImageWidth(width);
  this->dataPtr->camera->SetImageHeight(height);

  // \todo(nkoeng) these parameters via sdf
  this->dataPtr->camera->SetAntiAliasing(2);

  auto angle = cameraElem->Get<double>("horizontal_fov", 0);
  if (angle.first < 0.01 || angle.first > IGN_PI*2)
  {
    ignerr << "Invalid horizontal field of view [" << angle.first << "]\n";

    return false;
  }
  this->dataPtr->camera->SetAspectRatio(static_cast<double>(width)/height);
  this->dataPtr->camera->SetHFOV(angle.first);

  if (cameraElem->HasElement("distortion"))
  {
    // \todo(nkoenig) Port Distortion class
    // This->dataPtr->distortion.reset(new Distortion());
    // This->dataPtr->distortion->Load(this->sdf->GetElement("distortion"));
  }

  std::string formatStr = imgElem->Get<std::string>("format");
  ignition::common::Image::PixelFormatType format =
    ignition::common::Image::ConvertPixelFormat(formatStr);
  switch (format)
  {
    case ignition::common::Image::RGB_INT8:
      this->dataPtr->camera->SetImageFormat(ignition::rendering::PF_R8G8B8);
      break;
    default:
      ignerr << "Unsupported pixel format [" << formatStr << "]\n";
      break;
  }

  this->dataPtr->image = this->dataPtr->camera->CreateImage();

  this->Scene()->RootVisual()->AddChild(this->dataPtr->camera);

  // Create the directory to store frames
  if (cameraElem->HasElement("save") &&
      cameraElem->GetElement("save")->Get<bool>("enabled"))
  {
    sdf::ElementPtr elem = cameraElem->GetElement("save");
    this->dataPtr->saveImagePath = elem->Get<std::string>("path");
    this->dataPtr->saveImagePrefix = this->Name() + "_";
    this->dataPtr->saveImage = true;
  }

  return true;
}

//////////////////////////////////////////////////
void CameraSensorPrivate::RemoveCamera(rendering::ScenePtr _scene)
{
  if (_scene)
  {
    // \todo(nkoenig) Remove camera from scene!
  }
  this->camera = nullptr;
}

//////////////////////////////////////////////////
CameraSensor::CameraSensor()
  : dataPtr(new CameraSensorPrivate())
{
}

//////////////////////////////////////////////////
CameraSensor::~CameraSensor()
{
}

//////////////////////////////////////////////////
bool CameraSensor::Init()
{
  return this->Sensor::Init();
}

//////////////////////////////////////////////////
bool CameraSensor::Load(sdf::ElementPtr _sdf)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  // Check if this is being loaded via "builtin" or via a plugin
  if (_sdf->GetName() == "sensor")
  {
    if (!_sdf->GetElement("camera"))
    {
      ignerr << "<sensor><camera> SDF element not found while attempting to "
        << "load a ignition::sensors::CameraSensor\n";
      return false;
    }
  }

  if (!Sensor::Load(_sdf))
  {
    return false;
  }

  this->dataPtr->pub =
      this->dataPtr->node.Advertise<ignition::msgs::Image>(
          this->Topic());
  if (!this->dataPtr->pub)
    return false;

  if (this->Scene())
  {
    this->CreateCamera();
  }

  this->dataPtr->sceneChangeConnection =
      RenderingEvents::ConnectSceneChangeCallback(
      std::bind(&CameraSensor::SetScene, this, std::placeholders::_1));

  this->dataPtr->initialized = true;
  return true;
}

/////////////////////////////////////////////////
ignition::common::ConnectionPtr CameraSensor::ConnectImageCallback(
    std::function<void(const ignition::msgs::Image &)> _callback)
{
  return this->dataPtr->imageEvent.Connect(_callback);
}

/////////////////////////////////////////////////
void CameraSensor::SetScene(ignition::rendering::ScenePtr _scene)
{
  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
  // APIs make it possible for the scene pointer to change
  if (this->Scene() != _scene)
  {
    this->dataPtr->RemoveCamera(this->Scene());
    RenderingSensor::SetScene(_scene);
    if (this->dataPtr->initialized)
      this->CreateCamera();
  }
}

//////////////////////////////////////////////////
bool CameraSensor::Update(const ignition::common::Time &_now)
{
  if (!this->dataPtr->initialized)
  {
    ignerr << "Not initialized, update ignored.\n";
    return false;
  }

  if (!this->dataPtr->camera)
  {
    ignerr << "Camera doesn't exist.\n";
    return false;
  }

  std::lock_guard<std::mutex> lock(this->dataPtr->mutex);

  // move the camera to the current pose
  this->dataPtr->camera->SetLocalPose(this->Pose());

  // generate sensor data
  this->dataPtr->camera->Capture(this->dataPtr->image);

  unsigned int width = this->dataPtr->camera->ImageWidth();
  unsigned int height = this->dataPtr->camera->ImageHeight();
  unsigned char *data = this->dataPtr->image.Data<unsigned char>();

  ignition::common::Image::PixelFormatType format;
  switch (this->dataPtr->camera->ImageFormat())
  {
    case ignition::rendering::PF_R8G8B8:
      format = ignition::common::Image::RGB_INT8;
      break;
    default:
      ignerr << "Unsupported pixel format ["
        << this->dataPtr->camera->ImageFormat() << "]\n";
      break;
  }

  // create message
  ignition::msgs::Image msg;
  msg.set_width(width);
  msg.set_height(height);
  msg.set_step(width * rendering::PixelUtil::BytesPerPixel(
               this->dataPtr->camera->ImageFormat()));
  msg.set_pixel_format(format);
  msg.mutable_header()->mutable_stamp()->set_sec(_now.sec);
  msg.mutable_header()->mutable_stamp()->set_nsec(_now.nsec);
  msg.set_data(data, this->dataPtr->camera->ImageMemorySize());

  // publish
  this->dataPtr->pub.Publish(msg);

  // Trigger callbacks.
  try
  {
    this->dataPtr->imageEvent(msg);
  }
  catch(...)
  {
    ignerr << "Exception thrown in an image callback.\n";
  }

  // Save image
  if (this->dataPtr->saveImage)
  {
    this->dataPtr->SaveImage(data, width, height, format);
  }

  return true;
}

//////////////////////////////////////////////////
bool CameraSensorPrivate::SaveImage(const unsigned char *_data,
    unsigned int _width, unsigned int _height,
    ignition::common::Image::PixelFormatType _format)
{
  // Attempt to create the directory if it doesn't exist
  if (!ignition::common::isDirectory(this->saveImagePath))
  {
    if (!ignition::common::createDirectories(this->saveImagePath))
      return false;
  }

  std::string filename = this->saveImagePrefix +
                         std::to_string(this->saveImageCounter) + ".png";
  ++this->saveImageCounter;

  ignition::common::Image localImage;
  localImage.SetFromData(_data, _width, _height, _format);

  localImage.SavePNG(
      ignition::common::joinPaths(this->saveImagePath, filename));
  return true;
}

//////////////////////////////////////////////////
unsigned int CameraSensor::ImageWidth() const
{
  return this->dataPtr->camera->ImageWidth();
}

//////////////////////////////////////////////////
unsigned int CameraSensor::ImageHeight() const
{
  return this->dataPtr->camera->ImageHeight();
}

//////////////////////////////////////////////////
rendering::CameraPtr CameraSensor::RenderingCamera() const
{
  return this->dataPtr->camera;
}

IGN_COMMON_REGISTER_SINGLE_PLUGIN(
   ignition::sensors::SensorTypePlugin<CameraSensor>,
   ignition::sensors::SensorPlugin)

