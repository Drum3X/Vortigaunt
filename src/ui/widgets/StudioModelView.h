#pragma once

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>

#include <assimp/matrix4x4.h>

#include <memory>
#include <vector>

class QOpenGLShaderProgram;
class QTimer;
class StudioModel;

class StudioModelView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    explicit StudioModelView(QWidget* parent = nullptr);
    ~StudioModelView() override;

    int addModel(std::shared_ptr<const StudioModel> model, int mergeInto = -1);
    void clearModels();
    int modelCount() const { return static_cast<int>(m_models.size()); }
    std::shared_ptr<const StudioModel> model(int index) const;

    void setSequence(int model, int sequence);
    int sequence(int model) const;
    void setGaitSequence(int model, int sequence);
    void setAimPitch(int model, float degrees);
    void setPlaying(bool playing);
    bool isPlaying() const { return m_playing; }

    void setBoneOffset(int model, int bone, const aiMatrix4x4& offset);
    aiMatrix4x4 boneTransform(int model, int bone) const;

    void fitView(int onlyModel = -1);
    void setCameraAngles(float yaw, float pitch);

    QString placeholder;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct Mesh
    {
        int texture = -1;
        int first = 0;  // in the model's vertex buffer
        int count = 0;
    };

    struct Model
    {
        std::shared_ptr<const StudioModel> data;
        int mergeInto = -1;
        std::vector<int> mergeBones;  
        int sequence = 0;
        int gaitSequence = -1;
        float aimPitch = 0.0f;
        int offsetBone = -1;
        aiMatrix4x4 offset;
        std::vector<aiMatrix4x4> bones;  
        std::vector<Mesh> meshes;
        std::vector<unsigned int> textures; 
        unsigned int vao = 0;
        unsigned int vbo = 0;
        bool uploaded = false;
    };

    void updateBones();
    void buildVertices(const Model& model, std::vector<float>& out) const;
    void upload(Model& model);
    void release(Model& model);
    void releaseAll();
    void drawModel(Model& model, bool additivePass);
    bool bounds(QVector3D& low, QVector3D& high, int onlyModel = -1) const;

    std::vector<Model> m_models;
    QOpenGLShaderProgram* m_program = nullptr;
    bool m_glReady = false;
    bool m_fitPending = false;

    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    bool m_playing = true;
    float m_time = 0.0f;

    QVector3D m_target;
    float m_distance = 100.0f;
    float m_yaw = 30.0f; 
    float m_pitch = 15.0f;
    QPoint m_lastMouse;
    Qt::MouseButton m_dragButton = Qt::NoButton;
    QMatrix4x4 m_projection;
};
