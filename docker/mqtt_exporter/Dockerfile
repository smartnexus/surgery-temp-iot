FROM python:3.10-alpine

WORKDIR /mqtt-exporter

RUN pip3 install --upgrade pip
COPY __init__.py ./
COPY requirements.txt ./
RUN pip3 install -r requirements.txt
COPY main.py ./

EXPOSE 9000

#ENTRYPOINT ["tail", "-f", "/dev/null"]
CMD [ "python3", "-u", "./main.py" ]