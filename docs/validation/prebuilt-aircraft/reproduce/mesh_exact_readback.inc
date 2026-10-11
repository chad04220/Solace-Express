  // Diagnostic only. Exact uploaded bytes, including normals/material/AO, every rigid part,
  // the moving hull, and the GPU's full per-instance matrix. No geometry writes.
  if(const char* dump=getenv("REVIEW_MESH_DUMP")) {
    static std::string lastExactDump;
    if(lastExactDump!=dump && fp.plane.on){
      lastExactDump=dump;updatePartPoses(fp);
      const auto it=planeMeshes.find(hullKey(fp,fp.plane.PS[3]>.5f?1:0));
      if(it!=planeMeshes.end()){
        const auto& pm=it->second;const std::string base=std::string(dump)+".exact";
        auto bytes=[&](const std::string&name,const void*data,size_t size){FILE*f=fopen((base+name).c_str(),"wb");if(!f||fwrite(data,1,size,f)!=size){fprintf(stderr,"Exact readback write failed\n");abort();}fclose(f);};
        auto buffer=[&](const std::string&name,GLuint vbo){GLint size=0;glBindBuffer(GL_COPY_READ_BUFFER,vbo);glGetBufferParameteriv(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE,&size);std::vector<unsigned char>b(size_t(std::max(0,size)));if(size)glGetBufferSubData(GL_COPY_READ_BUFFER,0,size,b.data());glBindBuffer(GL_COPY_READ_BUFFER,0);bytes(name,b.data(),b.size());};
        buffer(".static.vertices",pm.vbo);buffer(".static.indices",pm.ibo);
        for(const auto&p:pm.parts){const auto part=".part"+std::to_string(p.type);buffer(part+".vertices",p.vbo);buffer(part+".indices",p.ibo);float centre[3]={p.c.x,p.c.y,p.c.z};bytes(part+".centre",centre,sizeof centre);}
        const auto h=hulls.find(pm.movKey);if(h!=hulls.end())buffer(".moving_hull.vertices",h->second.vbo);
        const uint32_t meta[]={uint32_t(pm.idx),uint32_t(pm.eyeInMov),uint32_t(pm.parts.size()),uint32_t(h==hulls.end()?0:h->second.verts)};bytes(".meta",meta,sizeof meta);
        const auto&O=poseOwner[0];const int n=int(poseType.size());std::vector<float>poses(size_t(n)*16);GLint prev=0;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&prev);
        if(n&&fboPartPose){glBindFramebuffer(GL_FRAMEBUFFER,fboPartPose);glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(0,0,n*4,1,GL_RGBA,GL_FLOAT,poses.data());glBindFramebuffer(GL_FRAMEBUFFER,GLuint(prev));
          bytes(".pose_matrices",poses.data()+size_t(O.base)*16,size_t(O.n)*16*sizeof(float));bytes(".pose_types",poseType.data()+O.base,size_t(O.n)*sizeof(poseType[0]));}
        printf("REVIEW_EXACT_READBACK %s\n",base.c_str());
      }
    }
  }
