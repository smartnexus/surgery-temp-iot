FROM python:3.10-alpine

WORKDIR /mqtt-exporter

RUN pip3 install --upgrade pip
COPY __init__.py ./
COPY requirements.txt ./
RUN pip3 install -r requirements.txt
COPY main.py ./

EXPOSE 9000

#ENTRYPOINT [ "python3" ]
#CMD [ "./main.py" ]
ENTRYPOINT ["tail", "-f", "/dev/null"]